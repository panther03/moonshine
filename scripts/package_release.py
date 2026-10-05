"""Build or verify the four V2.3.3 downloads from a checked, committed build.

`release` and `verify` require current full-ISO and runtime receipts. `candidate`
is the CI path without retail ISOs or a live emulator: it checks sources, host
tests and generated binaries, and explicitly records the missing runtime scope.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile
import zlib

import package_launcher
from gen_iso_bps import build_patch
from gen_japanese_ui import build as build_japanese_ui
from gen_mod_bin import build_mod_bin, shared_int_define

ROOT = Path(__file__).resolve().parents[1]
VERSION = "V2.3.3 Frame By Frame"
TIMER_BLOB = "c31dda5ddc9d5b1a0cfc3fd985f8d907fa4f5890"
HOST_TEST_FLOOR = 1239
REGIONS = ("jp", "us", "pal")
GAME_IDS = {"jp": 0x474D534A, "us": 0x474D5345, "pal": 0x474D5350}
FILES = {
    "CHANGELOG.md": "doc/release-notes-v2.3.3.md",
    "TESTING.md": "doc/v2.3.3-testing.md",
    "guide-en.md": "doc/guide-en.md",
    "guide-ja.md": "doc/guide-ja.md",
    "tools/decode_crash.py": "scripts/decode_crash.py",
    "licenses/Moonshine-LICENSE.txt": "LICENSE",
    "licenses/miniz-LICENSE.txt": "vendor/miniz/LICENSE",
    "licenses/lz4-LICENSE.txt": "vendor/lz4/LICENSE",
    "licenses/OFL-NotoSansCJK.txt": "launcher/loader/data/OFL-NotoSansCJK.txt",
    "licenses/Droid-LICENSE.txt": "data/fonts/Droid-LICENSE.txt",
    "licenses/fonts-README.md": "data/fonts/README.md",
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def relative(path):
    return Path(path).resolve().relative_to(ROOT.resolve()).as_posix()


def record(path):
    path = Path(path).resolve()
    data = path.read_bytes()
    return {"path": relative(path), "bytes": len(data), "sha256": sha(data)}


def evidence_path(name):
    path = PurePosixPath(name)
    require(not path.is_absolute() and ".." not in path.parts and "\\" not in name,
            "Evidence path must be repository-relative: " + name)
    root = ROOT.resolve()
    resolved = (root / name).resolve()
    require(resolved.is_relative_to(root), "Evidence escaped repository")
    return resolved


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def required_source_paths():
    """Shared with receipt collectors; captures code, shared layouts and generators."""
    tracked = git("ls-files", "src", "include", "launcher", "scripts", "data").splitlines()
    return sorted(path for path in tracked if (
        (path.startswith(("src/", "include/", "launcher/")) and
         Path(path).suffix in (".cpp", ".c", ".h", ".hxx", ".inc", ".S", ".s")) or
        (path.startswith("scripts/gen_") and path.endswith(".py")) or
        path in ("data/japanese_ui.tsv", "data/fonts/droid-japanese-codepoints.json",
                 "data/fonts/noto-japanese-supplement.json")))


def checked_test_log(text, allow_skipped=False):
    matches = re.findall(r"^Ran (\d+) tests?\b", text, re.M)
    require(matches and int(matches[-1]) >= HOST_TEST_FLOOR, "Final host suite is incomplete")
    footer = re.search(r"^Ran \d+ tests?[^\n]*\n\s*\n?OK(?: \(skipped=(\d+)\))?\s*\Z", text, re.M)
    require(footer, "Host suite did not end with OK")
    require(not re.search(r"^(?:FAILED|ERROR:|FAIL:)", text, re.M), "Host log contains failures")
    total, skipped = int(matches[-1]), int(footer.group(1) or 0)
    require(0 <= skipped <= total and (not skipped or allow_skipped),
            "Final verification cannot use skipped host tests")
    return {"total": total, "passed": total-skipped, "skipped": skipped}


def checked_dol(data):
    require(len(data) >= 256, "Launcher DOL header is truncated")
    ranges, text_ranges = [], []
    for index in range(18):
        offset, address, size = (struct.unpack_from(">I", data, base + index * 4)[0]
                                 for base in (0, 0x48, 0x90))
        if not size:
            continue
        require(offset >= 256 and offset + size <= len(data), "Launcher DOL section exceeds file")
        require(0x80000000 <= address < address + size <= 0x81800000,
                "Launcher DOL section exceeds MEM1")
        require(all(offset + size <= start or end <= offset for start, end in ranges),
                "Launcher DOL sections overlap in file")
        ranges.append((offset, offset + size))
        if index < 7:
            text_ranges.append((address, address + size))
    entry = struct.unpack_from(">I", data, 0xE0)[0]
    require(text_ranges and any(start <= entry < end for start, end in text_ranges),
            "Launcher entry point is outside executable sections")


def checked_patch(data, row):
    require(len(data) >= 16 and data[:4] == b"BPS1", "Invalid BPS header")
    source, target, checksum = struct.unpack("<III", data[-12:])
    require(zlib.crc32(data[:-4]) == checksum, "BPS checksum differs")
    require(sha(data) == row["patch_sha256"], "BPS differs from its full-ISO proof")
    for key, value in (("source_crc32", source), ("target_crc32", target), ("patch_crc32", checksum)):
        require(row[key] == f"{value:08X}", "BPS proof differs: " + key)


def checked_iso_row(row, manifest, layout, patch, asset, language):
    region = row["region"]
    require(region in REGIONS and manifest["game_id"] == GAME_IDS[region], "Proof region differs")
    checked_patch(patch, row)
    require(row["source_crc32"] == layout["source_crc32"] and row["size"] == layout["iso_size"],
            "Full-ISO proof uses a different source disc")
    require(row["hooks_verified"] == len(manifest["writes"]) >= 31, "Not every retail hook was verified")
    require(row.get("protected_hole_absent") is True and len(row["segments"]) == 2,
            "Protected MEM1 spans were not verified")
    for segment, verified in zip(manifest["segments"], row["segments"]):
        code = bytes.fromhex(segment["code"])
        raw = code + bytes(segment["memory_size"] - len(code))
        require(verified["sha256"] == sha(raw) and verified["runtime_bytes"] == len(raw) and
                verified["initialized_bytes"] == len(code) and
                int(verified["address"], 0) == manifest["base_addr"] + segment["offset"],
                "Full-ISO memory segment differs from the current build")
    if region == "jp":
        ui = row.get("japanese_ui", {})
        require(ui.get("offset") == 0x4AA8C0 and ui.get("extent") == 0x19000 and
                ui.get("outside_dol") is True and ui.get("ui_language") == language,
                "Japanese raw-disc extent was not verified")
        expected = asset if language == "ja" else bytes(0x19000)
        require(ui.get("bytes") == (len(asset) if language == "ja" else 0) and
                ui.get("sha256") == sha(expected) and ui.get("zeroed") is (language == "en"),
                "Japanese language asset differs from the selected download")
    else:
        require(language == "en" and "japanese_ui" not in row, "Unexpected non-JP translation")


def checked_runtime(proof, final_crc, japanese_crc, asset, host_tests):
    require(proof.get("passed") is True, "Current runtime proof is incomplete")
    require(proof.get("final_image_crc32") == final_crc and
            proof.get("japanese_image_crc32") == japanese_crc,
            "Runtime proof belongs to a different release")
    require(proof.get("asset_sha256") == sha(asset), "Runtime Japanese asset differs")
    require(proof.get("current_scopes") and isinstance(proof.get("historical_scopes"), list),
            "Current and historical runtime scopes must be stated separately")
    if "host_tests" in proof:
        require(proof["host_tests"] == host_tests, "Runtime receipt references a different host suite")
    sources = proof.get("current_sources", {})
    require(set(required_source_paths()) <= set(sources), "Runtime receipt is missing production sources")
    for path, digest in sources.items():
        require(sha(evidence_path(path).read_bytes()) == digest, "Runtime source changed: " + path)
    require(proof.get("evidence"), "Runtime receipt has no evidence files")
    for item in proof["evidence"]:
        require(record(evidence_path(item["path"])) == item, "Runtime evidence changed: " + item["path"])
    return proof


def validate_build(args):
    require(not git("status", "--porcelain"), "Commit the final source tree before packaging")
    require(git("hash-object", "src/qft_timer.cpp") == TIMER_BLOB, "Authorized QFT baseline changed")
    host = checked_test_log(args.host_log.read_text(encoding="utf-8-sig"), args.mode == "candidate")
    host_tests = host["total"]
    asset = bytes(build_japanese_ui()[0])
    require((args.build_dir / "ja_ui.bin").read_bytes() == asset, "Generated Japanese asset is stale")
    boot = args.launcher_boot.read_bytes()
    checked_dol(boot)
    checksum = 0
    memory, manifests, layouts, patches, artifacts = {}, {}, {}, {}, []
    for region in REGIONS:
        path = args.build_dir / f"mod_{region}.bin"
        manifest_path = args.build_dir / f"susamune_manifest_{region}.json"
        manifest = read_json(manifest_path)
        require(manifest["game_id"] == GAME_IDS[region], "Console mod region differs")
        mod = path.read_bytes()
        require(mod == build_mod_bin(manifest), "Console mod differs from its manifest: " + region)
        checksum = zlib.crc32(mod, checksum)
        spans = manifest["segments"]
        memory[region] = {"file_bytes": len(mod), "runtime_bytes": sum(s["memory_size"] for s in spans),
                          "low_free": 0x58000-spans[0]["memory_size"],
                          "upper_free": 0x40000-spans[1]["memory_size"]}
        emu_path = args.emulator_build_dir / f"susamune_manifest_{region}.json"
        emu = read_json(emu_path)
        require(emu["game_id"] == GAME_IDS[region], "Dolphin mod region differs")
        build_mod_bin(emu)
        layout_path = ROOT / f"data/iso_layout_{region}.json"
        layout = read_json(layout_path)
        for language in (("en", "ja") if region == "jp" else ("en",)):
            name = f"moonshine_{region}{'_ja' if language == 'ja' else ''}.bps"
            data = (args.emulator_build_dir / name).read_bytes()
            require(data == build_patch(layout, emu, language)[0], "Generated BPS is stale: " + name)
            patches[name] = data
            artifacts.append(record(args.emulator_build_dir / name))
        manifests[region], layouts[region] = emu, layout
        artifacts.extend(record(p) for p in (path, manifest_path, emu_path, layout_path))
    checksum = f"{checksum:08X}"
    header_path = args.build_dir / "launcher/shared/gen/susamune_build_id.h"
    header = header_path.read_text()
    require(re.search(r'#define\s+SUSAMUNE_BUILD_CHECKSUM\s+"' + checksum + r'"', header),
            "Launcher generated checksum is stale")
    require(checksum.encode("ascii") in boot, "Launcher does not contain the current mod checksum")
    abi = {}
    for name, expected, header_name in (
            ("SUSAMUNE_MOD_VERSION", 3, "mod_bin.h"),
            ("SUSAMUNE_CFG_VERSION", 2, "susamune_cfg.h"),
            ("SUSAMUNE_STATE_STORAGE_VERSION", 7, "state_storage.h"),
            ("SUSAMUNE_STATE_ARCHIVE_VERSION", 1, "state_storage.h"),
            ("SUSAMUNE_TAS_VERSION", 2, "tas_storage.h"),
            ("SUSAMUNE_MEM2_SNAPSHOT_SIZE", 0xFF0000, "mem2_map.h"),
            ("SUSAMUNE_STATE_POOL_EXTRA_SIZE", 0x200000, "mem2_map.h"),
            ("SUSAMUNE_STATE_CODEC_WORKSPACE_SIZE", 0x4E000, "mem2_map.h")):
        abi[name] = shared_int_define(name, header_name)
        require(abi[name] == expected, "Release ABI or capacity changed: " + name)
    artifacts.extend(record(p) for p in (args.launcher_boot, header_path, args.build_dir / "ja_ui.bin"))
    report = {"schema": 1, "version": VERSION, "source_commit": git("rev-parse", "HEAD"),
              "source_tree": git("rev-parse", "HEAD^{tree}"), "build_checksum": checksum,
              "authorized_qft_git_blob": TIMER_BLOB, "host_tests": host_tests, "abi": abi,
              "host_passed": host["passed"], "host_skipped": host["skipped"],
              "memory": memory, "asset_sha256": sha(asset), "artifacts": artifacts,
              "host_log": record(args.host_log), "new_wii_hardware_playtest": False}
    if args.mode == "candidate":
        report.update(final_verified=False,
                      validation_scope="source_build_checked; full_iso_and_runtime_not_checked")
    else:
        require(all((args.standard_proof, args.japanese_proof, args.runtime_proof)),
                "Final release requires standard, Japanese and current runtime proofs")
        rows = read_json(args.standard_proof)
        japanese = read_json(args.japanese_proof)
        require(len(rows) == 3 and {r["region"] for r in rows} == set(REGIONS), "All three ISO proofs are required")
        require(len(japanese) == 1 and japanese[0]["region"] == "jp", "Separate Japanese JP proof is required")
        for path, group, language in ((args.standard_proof, rows, "en"), (args.japanese_proof, japanese, "ja")):
            for row in group:
                region = row["region"]
                for name, current in ((f"{region}_manifest.json", args.emulator_build_dir / f"susamune_manifest_{region}.json"),
                                      (f"{region}_layout.json", ROOT / f"data/iso_layout_{region}.json")):
                    require((path.parent/name).read_bytes() == current.read_bytes(), "Full-ISO proof input changed: " + name)
                name = f"moonshine_{region}{'_ja' if language == 'ja' else ''}.bps"
                checked_iso_row(row, manifests[region], layouts[region], patches[name], asset, language)
        final_crc = {row["region"]: row["target_crc32"] for row in rows}
        runtime = checked_runtime(read_json(args.runtime_proof), final_crc,
                                  japanese[0]["target_crc32"], asset, host_tests)
        report.update(final_verified=True, validation_scope="current_build_full_iso_and_scoped_runtime_verified",
                      final_image_crc32=final_crc, japanese_image_crc32=japanese[0]["target_crc32"],
                      runtime=runtime, evidence=[record(p) for p in
                          (args.standard_proof, args.japanese_proof, args.runtime_proof)])
    return report, patches


def render_readme(kind, language, checksum, patches):
    japanese = language == "ja"
    title = f"Moonshine {'Launcher' if kind == 'launcher' else 'Dolphin'} — {VERSION}"
    if japanese:
        title += " 日本語版"
        body = f"# {title}\n\nビルド {checksum}\n\n"
        if kind == "launcher":
            body += ("Supports US, PAL and JP Sunshine. The launcher is Japanese; Moonshine's in-game menus "
                     "are Japanese on JP and English on US/PAL. The retail game's language is unchanged.\n\n"
                     "ZIP内の `apps` をSDカードのルートにコピーしてください。既存のアプリは更新しますが、"
                     "設定、記録、ゴースト、保存したステート、TASプロジェクトは削除しないでください。"
                     "初回起動時に既存のデータを `/Moonshine data` に移行します。設定は `/Moonshine data/moonshine.ini` に保存されます。\n\n"
                     "日本国旗の背景を使う場合だけ `Moonshine data/theme/background.png` もコピーしてください。"
                     "既存のテーマを残す場合はコピーしないでください。音楽は含まれません。\n\n"
                     "Homebrew ChannelからMoonshine Launcherを起動し、ゲームの版と場所を選んでください。"
                     "ランチャーは日本語、JP版SunshineのMoonshineメニューも日本語になります。"
                     "US/PALのゲーム内メニューは英語です。地域の選択で言語は切り替わりません。\n\n")
        else:
            body += ("For JP Sunshine only, with Japanese Moonshine menus.\n\n"
                     "`moonshine_jp_ja.bps` を、ご自身の変更していないJP版Sunshine ISO（GMSJ01）に適用し、"
                     "作成されたISOをDolphinで開いてください。元のISOは保存してください。"
                     "日本語フォントはパッチに含まれています。\n\n"
                     "SDステート・TASプロジェクトの保存機能にはWiiのMoonshine Launcherが必要です。"
                     "Dolphinでは通常のゲーム保存にスロットA、Moonshine設定にスロットBを使ってください。\n\n")
        body += ("操作方法は `guide-ja.md`、英語ガイドは `guide-en.md`、変更内容は `CHANGELOG.md` を参照してください。"
                 "ランチャー版ではこれらのファイルは `apps/moonshine_launcher` 内にあります。"
                 "一部の診断表示とランチャー内のガイド本文は英語です。\n")
    else:
        body = f"# {title}\n\nBuild {checksum}\n\n"
        body += ("Supports US, PAL and JP Sunshine, with English Moonshine menus in all three regions. "
                 "English refers to the mod's menu language; the retail game's language is unchanged.\n\n")
        if kind == "launcher":
            body += ("Copy `apps` to your SD card's root and replace the app files. Keep your settings, records, "
                     "ghosts, saved states, TAS projects and your theme. On first launch, existing data moves into "
                     "`/Moonshine data`; settings live in `/Moonshine data/moonshine.ini`. Open Moonshine Launcher from the Homebrew "
                     "Channel, then select your game region and disc or clean game image.\n\n"
                     "The separate JAPANESE-MENUS launcher download has a Japanese launcher, Japanese Moonshine "
                     "menus on JP, and English Moonshine menus on US/PAL. It includes an optional Japanese flag background.\n\n"
                     "Read `apps/moonshine_launcher/guide-en.md` for controls, `guide-ja.md` for the Japanese guide, "
                     "and `CHANGELOG.md` for release notes in the same folder.\n")
        else:
            body += ("Apply the matching BPS patch to your own clean retail Sunshine ISO, then open the patched copy "
                     "in Dolphin. Keep your original ISO. The separate JAPANESE-MENUS Dolphin download provides "
                     "Japanese Moonshine menus for JP Sunshine only.\n\n"
                     "Use slot A for ordinary game saves and slot B for Moonshine settings. SD-state and TAS-project "
                     "file storage requires the Wii launcher and is unavailable in these standalone Dolphin builds.\n\n"
                     "Read `guide-en.md` for controls, `guide-ja.md` for the Japanese guide, and `CHANGELOG.md` for release notes.\n")
    if kind == "dolphin":
        body += "\n| Patch | Clean ISO CRC32 | Patched ISO CRC32 |\n|---|---|---|\n"
        for name, data in sorted(patches.items()):
            source, target = struct.unpack("<II", data[-12:-4])
            body += f"| {name} | {source:08X} | {target:08X} |\n"
    return body.encode("utf-8")


def archive_contents(args, report, patches):
    common = {name: (ROOT/path).read_bytes() for name, path in FILES.items()}
    for name in ("CHANGELOG.md", "guide-en.md", "guide-ja.md"):
        require(not re.search(r"FOXTROT|\bRC1\b|pre-release|プレリリース", common[name].decode("utf-8"), re.I),
                "Old release branding remains in " + name)
    flag = (ROOT / "launcher/themes/japanese/background.png").read_bytes()
    require(flag[:8] == b"\x89PNG\r\n\x1a\n" and struct.unpack(">II", flag[16:24]) == (1024, 480),
            "Japanese flag background is invalid")
    result = {}
    for language in ("en", "ja"):
        app = package_launcher.launcher_files(args.launcher_boot,
            [args.build_dir/f"mod_{region}.bin" for region in REGIONS], version=VERSION,
            language=language, japanese_ui=args.build_dir/"ja_ui.bin")
        app.update({f"{package_launcher.APP_NAME}/{name}": data for name, data in common.items()})
        meta = ET.fromstring(app[f"{package_launcher.APP_NAME}/meta.xml"])
        require(meta.findtext("name") == "Moonshine Launcher" and meta.findtext("version") == VERSION,
                "Launcher metadata branding differs")
        app["README.md"] = render_readme("launcher", language, report["build_checksum"], {})
        if language == "ja":
            app["Moonshine data/theme/background.png"] = flag
        label = "ENGLISH-MENUS" if language == "en" else "JAPANESE-MENUS"
        name = f"Moonshine_{label}_Launcher_V2.3.3_US-PAL-JP.zip"
        result[name] = (language, "launcher", app)
        selected = {name: data for name, data in patches.items()
                    if (name == "moonshine_jp_ja.bps") == (language == "ja")}
        dolphin = {**common, **selected, "language.txt": (language+"\n").encode("ascii"),
                   "README.md": render_readme("dolphin", language, report["build_checksum"], selected)}
        regions = "US-PAL-JP" if language == "en" else "JP"
        name = f"Moonshine_{label}_Dolphin_V2.3.3_{regions}.zip"
        result[name] = (language, "dolphin", {"moonshine_dolphin/"+n: d for n, d in dolphin.items()})
    return result


def checked_zip(path, files):
    with zipfile.ZipFile(path) as archive:
        require(len(archive.namelist()) == len(files) and set(archive.namelist()) == set(files),
                "ZIP has missing, duplicate or unexpected files: " + path.name)
        require(archive.testzip() is None, "ZIP integrity failure: " + path.name)
        for name, data in files.items():
            require(archive.read(name) == data, "ZIP member differs: " + name)
    return {name: {"bytes": len(data), "sha256": sha(data)} for name, data in sorted(files.items())}


def write_zip(path, files):
    temporary = path.with_suffix(".zip.tmp")
    with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(name, date_time=(2026, 9, 10, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)
    checked_zip(temporary, files)
    temporary.replace(path)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", nargs="?", default="release", choices=("release", "verify", "candidate"))
    parser.add_argument("--build-dir", type=Path, default=ROOT/"build")
    parser.add_argument("--emulator-build-dir", type=Path, default=ROOT/"build/emu")
    parser.add_argument("--launcher-boot", type=Path, default=ROOT/"build/launcher/shared/boot.dol")
    parser.add_argument("--host-log", required=True, type=Path)
    parser.add_argument("--standard-proof", type=Path)
    parser.add_argument("--japanese-proof", type=Path)
    parser.add_argument("--runtime-proof", type=Path)
    parser.add_argument("--out-dir", type=Path, default=ROOT/"build/release-v2.3.3")
    args = parser.parse_args(argv)
    report, patches = validate_build(args)
    contents = archive_contents(args, report, patches)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    reports = {}
    for name, (language, kind, files) in contents.items():
        path = args.out_dir/name
        if args.mode != "verify":
            write_zip(path, files)
        members = checked_zip(path, files)
        digest = sha(path.read_bytes())
        reports[name] = {"kind": kind, "language": language, "bytes": path.stat().st_size,
                         "sha256": digest, "files": members}
        checksum = f"{digest}  {name}\n"
        if args.mode == "verify":
            require(path.with_suffix(".zip.sha256").read_text(encoding="utf-8") == checksum,
                    "ZIP SHA256 sidecar differs")
        else:
            path.with_suffix(".zip.sha256").write_text(checksum, encoding="utf-8")
    report["packages"] = reports
    encoded = json.dumps(report, ensure_ascii=False, indent=2)+"\n"
    manifest = args.out_dir/"release-manifest.json"
    if args.mode == "verify":
        require(read_json(manifest) == report, "Release manifest differs from verified current artifacts")
    else:
        manifest.write_text(encoded, encoding="utf-8")
    print(json.dumps({"final_verified": report["final_verified"], "build_checksum": report["build_checksum"],
                      "packages": {name: row["sha256"] for name, row in reports.items()}},
                     ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
