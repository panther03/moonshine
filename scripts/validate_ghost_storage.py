#!/usr/bin/env python3
"""Validate/select the fixed A/B files used by the console ghost service."""

from __future__ import annotations

import argparse
from datetime import date
from pathlib import Path
import struct
import sys
import zlib

import validate_ghost as ghost_format


ENVELOPE_MAGIC = 0x5347454E
ENVELOPE_VERSION = 2
ENVELOPE_SIZE = 64
TOMBSTONE = 0x00000001
PERSONAL_SLOT_AUTO = 0xFFFFFFFF
IMPORTED_PROFILE = 4
CATALOG_PAGE_ENTRIES = 16
SHARE_DIRECTORY = "share"
IMPORT_DIRECTORY = "import"
IMPORT_LEAF_SIZE = 96
SHARE_EXTENSION = ".smsghost"
ROUTE_ABBREVIATIONS = {
    0x00: "AS",
    0x01: "DP",
    0x02: "BH",
    0x03: "RH",
    0x04: "GB",
    0x05: "PP",
    0x06: "SB",
    0x08: "PV",
    0x09: "NB",
    0x34: "CM",
    0x3C: "BW",
}
_ENVELOPE = struct.Struct(">IHHIIIHHIIII6I")


class StorageError(ValueError):
    """The fixed-slot file is corrupt or violates the console bounds."""


class UnsupportedStorage(StorageError):
    """The file is recognized but must not be changed by this service."""


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise StorageError(message)


def _crc32_zeroed(raw: bytes, start: int, end: int) -> int:
    work = bytearray(raw)
    work[start:end] = bytes(end - start)
    return zlib.crc32(work) & 0xFFFFFFFF


def _validate_canonical_ghost(
    raw: bytes, *, running_region: int | None = None,
) -> dict:
    # Recognize old/future SGHF from its prefix without interpreting its body.
    if len(raw) >= 6 and raw[:4] == ghost_format.GHOST_MAGIC:
        version = struct.unpack_from(">H", raw, 4)[0]
        if version not in (ghost_format.GHOST_VERSION_V3,
                           ghost_format.GHOST_VERSION_V4,
                           ghost_format.GHOST_VERSION_V5,
                           ghost_format.GHOST_VERSION_V6):
            raise UnsupportedStorage(
                f"unsupported ghost version {version}; storage supports 3, 4, 5 and 6"
            )
    try:
        if running_region is None:
            return ghost_format.validate_ghost(raw)
        return ghost_format.validate_imported_ghost(
            raw, running_region=running_region
        )
    except (ghost_format.UnsupportedVersion,
            ghost_format.UnsupportedFeature) as exc:
        raise UnsupportedStorage(str(exc)) from exc
    except ghost_format.FormatError as exc:
        raise StorageError(str(exc)) from exc


def validate_slot_file(
    raw: bytes, *, game_id: int, profile: int, slot: int
) -> dict:
    _require(len(raw) >= 6, "truncated storage envelope")
    magic, version = struct.unpack_from(">IH", raw)
    _require(magic == ENVELOPE_MAGIC, "bad storage envelope magic")
    if version not in (1, ENVELOPE_VERSION):
        raise UnsupportedStorage(f"unsupported storage envelope version {version}")
    _require(len(raw) >= ENVELOPE_SIZE, "truncated storage envelope")
    fields = _ENVELOPE.unpack_from(raw)
    (
        _magic, _version, header_size, generation, flags, stored_game_id,
        stored_profile, stored_slot, payload_size, duration_qf,
        payload_checksum, header_checksum, *reserved,
    ) = fields
    _require(header_size == ENVELOPE_SIZE, "invalid storage envelope size")
    _require(stored_game_id == game_id, "storage game id mismatch")
    _require(stored_profile == profile, "storage profile mismatch")
    _require(stored_slot == (slot & 0xFFFF), "storage slot mismatch")
    _require((version == 1 and slot <= 0xFFFF) or
             (version == 2 and reserved[0] == slot), "storage wide slot mismatch")
    _require(not flags & ~TOMBSTONE, "unknown storage envelope flags")
    _require(not any(reserved if version == 1 else reserved[1:]),
             "nonzero storage envelope reserved bytes")
    _require(
        header_checksum == _crc32_zeroed(raw[:ENVELOPE_SIZE], 36, 40),
        "storage envelope checksum mismatch",
    )
    payload = raw[ENVELOPE_SIZE:]
    _require(payload_size == len(payload), "storage payload size mismatch")

    if flags & TOMBSTONE:
        _require(not payload, "tombstone has a payload")
        _require(duration_qf == 0, "tombstone has a duration")
        _require(payload_checksum == 0, "tombstone has a payload checksum")
        ghost = None
    else:
        _require(
            payload_checksum == (zlib.crc32(payload) & 0xFFFFFFFF),
            "storage payload checksum mismatch",
        )
        ghost = _validate_canonical_ghost(payload)
        _require(int(ghost["game_id"], 16) == game_id, "ghost game id mismatch")
        _require(ghost["source_profile"] == profile, "ghost profile mismatch")
        _require(ghost["duration_qf"] == duration_qf,
                 "ghost duration does not match envelope")

    return {
        "generation": generation,
        "flags": flags,
        "payload_size": payload_size,
        "duration_qf": duration_qf,
        "payload_checksum": payload_checksum,
        "ghost": ghost,
    }


def _newer(candidate: int, current: int) -> bool:
    delta = (candidate - current) & 0xFFFFFFFF
    return 0 < delta < 0x80000000


def choose_slot(
    files: tuple[bytes | None, bytes | None],
    *, game_id: int, profile: int, slot: int,
) -> tuple[int, dict] | None:
    valid: list[tuple[int, dict]] = []
    for bank, raw in enumerate(files):
        if raw is None:
            continue
        try:
            valid.append((bank, validate_slot_file(
                raw, game_id=game_id, profile=profile, slot=slot
            )))
        except UnsupportedStorage:
            raise
        except StorageError:
            continue
    if not valid:
        return None
    if len(valid) == 1:
        return valid[0]

    a, b = valid
    delta = (b[1]["generation"] - a[1]["generation"]) & 0xFFFFFFFF
    if delta == 0:
        comparable = ("flags", "payload_size", "duration_qf", "payload_checksum")
        if any(a[1][key] != b[1][key] for key in comparable):
            raise UnsupportedStorage("conflicting equal storage generations")
        return a
    if delta == 0x80000000:
        raise UnsupportedStorage("ambiguous wrapped storage generations")
    return b if _newer(b[1]["generation"], a[1]["generation"]) else a


def save_status(*, occupied: bool, request_flags: int) -> str:
    if request_flags != 0:
        return "invalid_request"
    if occupied:
        return "slot_occupied"
    return "ok"


def personal_slot_is_live(slot: int) -> bool:
    return 0 <= slot < PERSONAL_SLOT_AUTO and slot not in (45, 46, 47)


def share_directory(region: str, profile: int) -> str:
    _require(region in ghost_format.REGION_NAMES.values(), "invalid region")
    _require(0 <= profile < 4, "invalid profile")
    return f"/Moonshine data/ghosts/{SHARE_DIRECTORY}/{region}/p{profile}"


def import_directory() -> str:
    return f"/Moonshine data/ghosts/{IMPORT_DIRECTORY}"


def validate_import_leaf(leaf: str) -> str:
    _require(0 < len(leaf) < IMPORT_LEAF_SIZE,
             "import leaf is empty or too long")
    _require(all(0x20 <= ord(char) <= 0x7E for char in leaf),
             "import leaf is not printable ASCII")
    _require(not any(char in '\"*/:<>?\\|' for char in leaf),
             "import leaf contains a path character")
    _require(not leaf.endswith((" ", ".")),
             "import leaf has an ambiguous FAT suffix")
    _require(len(leaf) > len(SHARE_EXTENSION) and
             leaf.lower().endswith(SHARE_EXTENSION),
             "import leaf does not end in .smsghost")
    return leaf


def sorted_import_leaves(leaves: list[str]) -> tuple[list[str], int]:
    valid = []
    for leaf in leaves:
        try:
            valid.append(validate_import_leaf(leaf))
        except StorageError:
            continue
    valid.sort(key=lambda leaf: (leaf.lower(), leaf))
    return valid, 0


def route_filename_label(ghost: dict) -> str:
    route = ghost["route"]
    area = route["area"]
    episode = route["episode"]
    if (route["parent_area"] != ghost_format.ROUTE_PARENT_NONE and
            route["variant"] >= 0):
        area = route["parent_area"]
        episode = route["variant"]
    abbreviation = ROUTE_ABBREVIATIONS.get(area)
    if abbreviation is not None:
        return f"{abbreviation}{episode + 1}"
    return f"A{area:02X}E{episode:02X}"


def compact_milliseconds(millis: int) -> str:
    _require(0 <= millis <= 900_000, "export time outside 15-minute bound")
    minutes, remainder = divmod(millis, 60_000)
    seconds, milliseconds = divmod(remainder, 1000)
    if minutes:
        return f"{minutes}{seconds:02d}{milliseconds:03d}"
    return f"{seconds}{milliseconds:03d}"


def duration_milliseconds(duration_qf: int) -> int:
    _require(0 <= duration_qf <= ghost_format.MAX_DURATION_QF,
             "export duration outside ghost bound")
    return duration_qf * 1001 // 120


def export_share_path(
    region: str, profile: int, ghost: dict, export_date: date,
) -> str:
    _require(1980 <= export_date.year <= 2107,
             "export date outside FAT timestamp range")
    route = route_filename_label(ghost)
    time = compact_milliseconds(duration_milliseconds(ghost["duration_qf"]))
    checksum = ghost["file_checksum"]
    _require(0 <= checksum <= 0xFFFFFFFF, "invalid export checksum")
    leaf = (
        f"{export_date:%Y_%m_%d}_{route}_{time}"
        f"[{checksum:08X}]{SHARE_EXTENSION}"
    )
    return f"{share_directory(region, profile)}/{leaf}"


def validate_share_file(raw: bytes, *, game_id: int, profile: int) -> dict:
    ghost = _validate_canonical_ghost(raw)
    _require(int(ghost["game_id"], 16) == game_id, "ghost game id mismatch")
    _require(ghost["source_profile"] == profile, "ghost profile mismatch")
    return ghost


def validate_import_file(raw: bytes, *, running_region: int) -> dict:
    return _validate_canonical_ghost(raw, running_region=running_region)


def select_import_files(
    candidates: list[tuple[str, bytes]], *, running_region: int,
) -> tuple[list[tuple[str, dict]], int]:
    valid = []
    for leaf, raw in candidates:
        try:
            valid.append((
                validate_import_leaf(leaf),
                validate_import_file(raw, running_region=running_region),
            ))
        except StorageError:
            continue
    valid.sort(key=lambda candidate: (candidate[0].lower(), candidate[0]))
    return valid, 0


def import_status(
    *, occupied: bool, count: int, total_duration_qf: int,
    import_duration_qf: int, request_flags: int = 0,
) -> str:
    if request_flags != 0:
        return "invalid_request"
    if occupied:
        return "slot_occupied"
    if not 0 < import_duration_qf <= ghost_format.MAX_DURATION_QF:
        return "invalid_file"
    return "ok"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("file", type=Path)
    parser.add_argument("--region", choices=ghost_format.REGION_NAMES.values(),
                        required=True)
    parser.add_argument("--profile", type=int, choices=range(4), required=True)
    parser.add_argument("--slot", type=int, required=True)
    args = parser.parse_args(argv)
    region = next(key for key, value in ghost_format.REGION_NAMES.items()
                  if value == args.region)
    try:
        _require(personal_slot_is_live(args.slot), "invalid personal slot")
        with args.file.open("rb") as source:
            raw = source.read(ENVELOPE_SIZE + ghost_format.MAX_GHOST_FILE_SIZE + 1)
        _require(
            len(raw) <= ENVELOPE_SIZE + ghost_format.MAX_GHOST_FILE_SIZE,
            "storage file exceeds fixed-slot limit",
        )
        result = validate_slot_file(
            raw,
            game_id=ghost_format.REGION_GAME_IDS[region],
            profile=args.profile,
            slot=args.slot,
        )
    except (OSError, StorageError) as exc:
        print(f"invalid: {exc}", file=sys.stderr)
        return 1
    print(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
