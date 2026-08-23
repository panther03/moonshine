"""Pack the mod manifest produced by `link_mod.py launcher` into mod_<vers>.bin,
the file the launcher ships next to boot.dol and loads at runtime.

Format is struct SusamuneModHeader from include/susamune/mod_bin.h: a 32-byte
big-endian header, the initialized image prefix, then the (addr, val) hook
writes. The kernel zeroes the omitted BSS tail up to memory_size on every
injection. The writes travel with the code because their addresses are
version-specific -- a blob on its own is not applicable to anything.

Usage: gen_mod_bin.py MANIFEST.json -o mod_jp.bin
"""
import argparse
import json
import re
import struct
import sys
from pathlib import Path

def shared_int_define(name, header_name="mem2_map.h"):
    header = (Path(__file__).parent.parent / "include" / "susamune" /
              header_name)
    match = re.search(
        r"^#define\s+{}\s+(0x[0-9a-fA-F]+|[0-9]+)u?"
        r"[ \t]*(?://[^\r\n]*)?$".format(
            re.escape(name)),
        header.read_text(), re.M)
    if not match:
        raise RuntimeError("{} not found in {}".format(name, header))
    return int(match.group(1), 0)


# These values define the wire format. Read the shared C header so the host
# packer cannot silently emit a different version or header layout.
MAGIC = shared_int_define("SUSAMUNE_MOD_MAGIC", "mod_bin.h")
VERSION = shared_int_define("SUSAMUNE_MOD_VERSION", "mod_bin.h")
HEADER_SIZE = shared_int_define("SUSAMUNE_MOD_HEADER_SIZE", "mod_bin.h")


# The loader refuses a larger file, so fail the build instead of shipping one
# that cannot be staged. Read the shared C header rather than duplicating it.
STAGING_WINDOW_SIZE = shared_int_define("SUSAMUNE_MEM2_MODBIN_SIZE")
STAGED_FILE_MAX_SIZE = shared_int_define("SUSAMUNE_MOD_STAGED_FILE_MAX_SIZE")
BLOB_MAX_SIZE = shared_int_define("SUSAMUNE_MOD_BLOB_MAX_SIZE", "mod_bin.h")


def build_mod_bin(manifest):
    code = bytes.fromhex(manifest["code"])
    declared_size = manifest.get("size", len(code))
    if type(declared_size) is not int or declared_size != len(code):
        raise ValueError("manifest code size is inconsistent")
    if len(code) % 4:
        raise ValueError("code blob is not word-aligned")
    memory_size = manifest.get("memory_size", len(code))
    if type(memory_size) is not int:
        raise ValueError("runtime image size is not an integer")
    if memory_size % 4:
        raise ValueError("runtime image is not word-aligned")
    if len(code) > memory_size:
        raise ValueError("initialized code exceeds the runtime image")
    if memory_size > BLOB_MAX_SIZE:
        raise ValueError(
            f"runtime image is {memory_size:#x} bytes, over the {BLOB_MAX_SIZE:#x} "
            "MEM1 working cap")

    writes = manifest["writes"]
    body = code + b"".join(
        struct.pack(">II", addr & 0xFFFFFFFF, val & 0xFFFFFFFF) for addr, val in writes)

    header = struct.pack(
        ">8I",
        MAGIC,
        VERSION,
        manifest["game_id"],
        manifest["base_addr"],
        len(code),
        len(writes),
        manifest.get("region_reserve", 0),
        memory_size,
    )
    assert len(header) == HEADER_SIZE
    total = HEADER_SIZE + len(body)
    if total > STAGED_FILE_MAX_SIZE:
        raise ValueError(
            f"mod bin is {total:#x} bytes, over the {STAGED_FILE_MAX_SIZE:#x} "
            "reset-safe ceiling (see SUSAMUNE_MOD_STAGED_FILE_MAX_SIZE)")
    if total > STAGING_WINDOW_SIZE:
        raise ValueError("mod bin exceeds its MEM2 staging window")
    return header + body


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("manifest", help="Mod manifest JSON from link_mod.py launcher")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args(argv)

    manifest = json.loads(Path(args.manifest).read_text())
    Path(args.output).write_bytes(build_mod_bin(manifest))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
