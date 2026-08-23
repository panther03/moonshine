"""Package the built Nintendont loader into the HBC app zip:
moonshine_launcher/{boot.dol, icon.png, meta.xml, mod_<region>.bin...}.

One app serves every supported disc revision. The mod is no longer compiled into
the launcher: each mod_<region>.bin sits next to boot.dol and the loader reads
the one matching the disc it detected (see launcher/loader/source/SusamuneMod.c),
which is why they are packaged here rather than embedded.

meta.xml is rendered from launcher/meta.xml.j2 (jinja2) with the git short hash.

Usage: package_launcher.py --boot-dol boot.dol --out-zip out.zip \
                           [--source di|sd|usb] [--test-log TESTING.md] \
                           [--changelog CHANGELOG.md] \
                           [--mod-bins mod_jp.bin ...]
"""
import argparse
import subprocess
import sys
import zipfile
from pathlib import Path

LAUNCHER_DIR = Path(__file__).resolve().parent.parent / "launcher"
META_TEMPLATE = LAUNCHER_DIR / "meta.xml.j2"
APP_NAME = "moonshine_launcher"
APP_ICON = LAUNCHER_DIR / "icon.png"


def git_version():
    """The tag name if HEAD is exactly at a tag (CI release builds), else the
    short commit hash."""
    repo_dir = str(LAUNCHER_DIR.parent)
    try:
        return subprocess.check_output(
            ["git", "describe", "--tags", "--exact-match", "HEAD"],
            cwd=repo_dir, text=True, stderr=subprocess.DEVNULL).strip()
    except Exception:
        pass
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=repo_dir, text=True).strip()
    except Exception:
        return "unknown"


def render_meta(source, regions, version=None):
    import jinja2
    template = jinja2.Template(META_TEMPLATE.read_text())
    return template.render(version=version or git_version(), source=source,
                           regions=regions)


def main(argv):
    ap = argparse.ArgumentParser(description="Package the Nintendont launcher HBC app zip.")
    ap.add_argument("--boot-dol", required=True, help="Built loader boot.dol")
    ap.add_argument("--out-zip", required=True, help="Output HBC app zip")
    ap.add_argument("--source", default="di", choices=["di", "sd", "usb"])
    ap.add_argument("--version", help="meta.xml version override")
    ap.add_argument("--test-log", help="tester log to include as TESTING.md")
    ap.add_argument("--changelog", help="release notes to include as CHANGELOG.md")
    ap.add_argument("--mod-bins", nargs="*", default=[],
                    help="mod_<region>.bin files to drop into the app dir")
    args = ap.parse_args(argv)

    mod_bins = [Path(p) for p in args.mod_bins]
    # "mod_jp.bin" -> "jp", for the meta.xml blurb.
    regions = sorted(p.stem.split("_", 1)[1] for p in mod_bins)

    with zipfile.ZipFile(args.out_zip, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(args.boot_dol, f"{APP_NAME}/boot.dol")
        z.write(APP_ICON, f"{APP_NAME}/icon.png")
        z.writestr(f"{APP_NAME}/meta.xml",
                   render_meta(args.source, regions, args.version))
        if args.test_log:
            z.write(args.test_log, f"{APP_NAME}/TESTING.md")
        if args.changelog:
            z.write(args.changelog, f"{APP_NAME}/CHANGELOG.md")
        for bin_path in mod_bins:
            z.write(bin_path, f"{APP_NAME}/{bin_path.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
