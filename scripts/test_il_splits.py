#!/usr/bin/env python3
"""Host contracts for the V7 IL split journal, migration, and overlay."""

from __future__ import annotations

from copy import deepcopy
import importlib.util
from pathlib import Path
import re
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]
CHECKPOINT_SCHEMA = ROOT / "scripts" / "split_checkpoint_schema.py"
HEADER = ROOT / "include" / "susamune" / "susamune_cfg.h"
MEM2 = ROOT / "include" / "susamune" / "mem2_map.h"
SPLITS = ROOT / "src" / "split_stats.cpp"
SPLIT_API = ROOT / "include" / "susamune" / "split_stats.hxx"
ILING_ENTRIES = ROOT / "src" / "iling_entries.inc"
QFT = ROOT / "src" / "qft_timer.cpp"
MENU = ROOT / "src" / "menu.cpp"
QFT_DISPLAY = ROOT / "src" / "qft_display.cpp"
CREATION = ROOT / "src" / "creation.cpp"
ACTION_SOURCE = ROOT / "src" / "actions.cpp"
SAVESTATE_SOURCE = ROOT / "src" / "savestate.cpp"
KERNEL = ROOT / "launcher" / "kernel" / "SusamuneCfg.c"

schema_spec = importlib.util.spec_from_file_location(
    "split_checkpoint_schema", CHECKPOINT_SCHEMA
)
assert schema_spec and schema_spec.loader
checkpoint_schema = importlib.util.module_from_spec(schema_spec)
schema_spec.loader.exec_module(checkpoint_schema)

MAGIC = 0x53535446
VERSION = 7
ROUTES = 122
SEGMENTS = 275
REGIONS = 3
PROFILES = 4
SCHEMA_HASH = checkpoint_schema.EXPECTED_SCHEMA_HASH
V2_SCHEMA_HASH = 0xE70B57F8
PR7_SCHEMA_HASH = 0xB9B6E310
UNSET = 0xFFFFFFFF
MAX_QF = 0x000AF9B0
PAYLOAD_SIZE = 0x6E34
FILE_SIZE = 0x6FE0
MAILBOX_SIZE = 0x7000
MAILBOX_OFFSET = 0x8800

V5_VERSION = 5
V5_ROUTES = 61
V5_SEGMENTS = 217
V5_SCHEMA_HASH = checkpoint_schema.EXPECTED_V5_SCHEMA_HASH
V5_PAYLOAD_SIZE = 0x46E0
V5_FILE_SIZE = 0x47E0
V5_ROUTE_FIRST = (
    0, 4, 7, 12, 16, 21, 24, 28, 32, 36,
    38, 41, 45, 47, 52, 56, 58, 63, 68, 73,
    76, 78, 83, 85, 89, 91, 94, 97, 103, 107,
    109, 111, 114, 117, 119, 123, 126, 130, 132, 134,
    137, 142, 145, 148, 154, 157, 163, 166, 169, 174,
    178, 181, 186, 190, 192, 197, 200, 202, 206, 211,
    215,
)
V5_ROUTE_COUNTS = (
    4, 3, 5, 4, 5, 3, 4, 4, 4, 2,
    3, 4, 2, 5, 4, 2, 5, 5, 5, 3,
    2, 5, 2, 4, 2, 3, 3, 6, 4, 2,
    2, 3, 3, 2, 4, 3, 4, 2, 2, 3,
    5, 3, 3, 6, 3, 6, 3, 3, 5, 4,
    3, 5, 4, 2, 5, 3, 2, 4, 5, 4,
    2,
)
ROUTE_COUNTS = tuple(len(row) + 1 for row in checkpoint_schema.CHECKPOINTS)
ROUTE_FIRST = tuple(
    sum(ROUTE_COUNTS[:route]) for route in range(ROUTES)
)
ROUTE_CHECKPOINTS = tuple(count - 1 for count in ROUTE_COUNTS)
ROUTE_ENTRIES = checkpoint_schema.ROUTE_ENTRIES
ROUTE_CATALOG_NAMES = (
    "Bianco 4", "Gelato GBS", "Pianta 6", "Ricco 1", "Ricco 2 (Full)",
    "Ricco 3", "Ricco 4 (Full)", "Ricco 5", "Ricco 6", "Ricco 7",
    "Airstrip 1", "Bianco Plant", "Delfino Shadow Mario", "Bianco 2",
    "Bianco 3 (Full)", "Bianco 3 (Secret)", "Travel Skip", "Bianco 5",
    "Bianco 6 (Full)", "Bianco 6 (Secret)", "Bianco 7", "Gelato Plant",
    "Gelato 7", "Pianta 1", "Pianta 2", "Pianta 3", "Pianta 4",
    "Pianta 5 (Full)", "Pianta 5 (Secret)", "Pianta 7", "Honey Skip",
    "Pinna 1", "Pinna 2 (Full)", "Pinna 2 (Secret)", "Pinna 3",
    "Pinna 4", "Pinna Park EYG", "Pinna 6 (Secret)", "Pinna 7",
    "Sirena 1", "Sirena 2 (Full)", "Sirena 2 (Secret)", "Sirena 3",
    "Sirena 4 (Full)", "Sirena 4 (Secret)", "Sirena 5", "Sirena 6",
    "Sirena 7", "Noki 1", "Noki 2", "Noki 3", "Noki 4",
    "Noki 4 (Eel Only)", "Noki 5", "Noki 6 (Full)",
    "Noki 6 (Secret)", "Noki 7", "Corona Mountain", "Bowser",
    "Ricco 2 (Race)", "Ricco 4 (Secret)",
)
FREEZE_FRAMES = (0, 15, 30, 60, 90, 150)

V1_VERSION = 1
V1_ROUTES = 3
V1_SEGMENTS = 9
V1_SCHEMA_HASH = 0x51ADB070
V1_ROUTE_FIRST = (0, 3, 5)
V1_ROUTE_COUNTS = (3, 2, 4)
V1_PAYLOAD_SIZE = 0x318
V1_FILE_SIZE = 0x340

V2_ROUTES = 10
V2_SEGMENTS = 38
V2_ROUTE_FIRST = V5_ROUTE_FIRST[:V2_ROUTES]
V2_ROUTE_COUNTS = V5_ROUTE_COUNTS[:V2_ROUTES]

V3_VERSION = 3
V3_ROUTES = 59
V3_SEGMENTS = 212
V3_SCHEMA_HASH = 0xD452F6FD
V3_PAYLOAD_SIZE = 0x450C
V3_FILE_SIZE = 0x4540
V3_ROUTE_FIRST = (
    0, 4, 7, 12, 16, 21, 24, 28, 32, 36,
    38, 41, 45, 47, 53, 57, 59, 64, 69, 74,
    77, 79, 84, 86, 90, 92, 95, 98, 104, 108,
    110, 112, 115, 118, 120, 125, 128, 131, 133, 135,
    138, 144, 148, 151, 157, 160, 166, 168, 171, 176,
    180, 183, 188, 192, 194, 199, 202, 204, 207,
)
V3_ROUTE_COUNTS = (
    4, 3, 5, 4, 5, 3, 4, 4, 4, 2,
    3, 4, 2, 6, 4, 2, 5, 5, 5, 3,
    2, 5, 2, 4, 2, 3, 3, 6, 4, 2,
    2, 3, 3, 2, 5, 3, 3, 2, 2, 3,
    6, 4, 3, 6, 3, 6, 2, 3, 5, 4,
    3, 5, 4, 2, 5, 3, 2, 3, 5,
)
V3_CHANGED_ROUTES = (5, 34, 36, 37, 46, 48, 49, 57)

V4_VERSION = 4
V4_ROUTES = 61
V4_SEGMENTS = 220
V4_SCHEMA_HASH = 0x2E5CC875
V4_PAYLOAD_SIZE = 0x4794
V4_FILE_SIZE = 0x47E0
V4_ROUTE_FIRST = (
    0, 4, 7, 12, 16, 21, 24, 28, 32, 36,
    38, 41, 45, 47, 53, 57, 59, 64, 69, 74,
    77, 79, 84, 86, 90, 92, 95, 98, 104, 108,
    110, 112, 115, 118, 120, 124, 127, 131, 133, 135,
    138, 144, 148, 151, 157, 160, 166, 169, 172, 177,
    181, 184, 189, 193, 195, 200, 203, 205, 209, 214,
    218,
)
V4_ROUTE_COUNTS = (
    4, 3, 5, 4, 5, 3, 4, 4, 4, 2,
    3, 4, 2, 6, 4, 2, 5, 5, 5, 3,
    2, 5, 2, 4, 2, 3, 3, 6, 4, 2,
    2, 3, 3, 2, 4, 3, 4, 2, 2, 3,
    6, 4, 3, 6, 3, 6, 3, 3, 5, 4,
    3, 5, 4, 2, 5, 3, 2, 4, 5, 4,
    2,
)
V4_REMOVED_CHECKPOINT = {40: 2, 41: 0}

FILE_HEADER = struct.Struct(">IHBBHBBIIII4s")


def hash_word(value: int, word: int) -> int:
    return ((value ^ word) * 16777619) & 0xFFFFFFFF


def generation_is_newer(candidate: int, current: int) -> bool:
    delta = (candidate - current) & 0xFFFFFFFF
    return delta != 0 and delta < 0x80000000


def schema_hash() -> int:
    return checkpoint_schema.schema_hash()


def blank_payload() -> dict[str, list]:
    return {
        "stats": [[[0, 0, 0] for _ in range(ROUTES)] for _ in range(REGIONS)],
        "played": [[0] * ROUTES for _ in range(REGIONS)],
        "best": [[UNSET] * SEGMENTS for _ in range(REGIONS)],
        "identity": [
            [[UNSET] * ROUTES for _ in range(PROFILES)] for _ in range(REGIONS)
        ],
        "pb": [
            [[UNSET] * SEGMENTS for _ in range(PROFILES)] for _ in range(REGIONS)
        ],
    }


def blank_v5_payload() -> dict[str, list]:
    return {
        "stats": [[[0, 0, 0] for _ in range(V5_ROUTES)]
                  for _ in range(REGIONS)],
        "best": [[UNSET] * V5_SEGMENTS for _ in range(REGIONS)],
        "identity": [
            [[UNSET] * V5_ROUTES for _ in range(PROFILES)]
            for _ in range(REGIONS)
        ],
        "pb": [
            [[UNSET] * V5_SEGMENTS for _ in range(PROFILES)]
            for _ in range(REGIONS)
        ],
    }


def blank_v2_payload() -> dict[str, list]:
    return {
        "stats": [[[0, 0, 0] for _ in range(V2_ROUTES)]
                  for _ in range(REGIONS)],
        "best": [[UNSET] * V2_SEGMENTS for _ in range(REGIONS)],
        "identity": [
            [[UNSET] * V2_ROUTES for _ in range(PROFILES)]
            for _ in range(REGIONS)
        ],
        "pb": [
            [[UNSET] * V2_SEGMENTS for _ in range(PROFILES)]
            for _ in range(REGIONS)
        ],
    }


def blank_v3_payload() -> dict[str, list]:
    return {
        "stats": [[[0, 0, 0] for _ in range(V3_ROUTES)]
                  for _ in range(REGIONS)],
        "best": [[UNSET] * V3_SEGMENTS for _ in range(REGIONS)],
        "identity": [
            [[UNSET] * V3_ROUTES for _ in range(PROFILES)]
            for _ in range(REGIONS)
        ],
        "pb": [
            [[UNSET] * V3_SEGMENTS for _ in range(PROFILES)]
            for _ in range(REGIONS)
        ],
    }


def blank_v4_payload() -> dict[str, list]:
    return {
        "stats": [[[0, 0, 0] for _ in range(V4_ROUTES)] for _ in range(REGIONS)],
        "best": [[UNSET] * V4_SEGMENTS for _ in range(REGIONS)],
        "identity": [
            [[UNSET] * V4_ROUTES for _ in range(PROFILES)] for _ in range(REGIONS)
        ],
        "pb": [
            [[UNSET] * V4_SEGMENTS for _ in range(PROFILES)]
            for _ in range(REGIONS)
        ],
    }


def payload_words(payload: dict[str, list]) -> list[int]:
    words: list[int] = []
    for region in range(REGIONS):
        for route in range(ROUTES):
            words.extend(payload["stats"][region][route])
    for region in range(REGIONS):
        words.extend(payload["played"][region])
    for region in range(REGIONS):
        words.extend(payload["best"][region])
    for region in range(REGIONS):
        for profile in range(PROFILES):
            words.extend(payload["identity"][region][profile])
    for region in range(REGIONS):
        for profile in range(PROFILES):
            words.extend(payload["pb"][region][profile])
    assert len(words) * 4 == PAYLOAD_SIZE
    return words


def checksum(payload: dict[str, list], generation: int,
             schema: int = SCHEMA_HASH) -> int:
    value = 2166136261
    value = hash_word(value, (VERSION << 16) | (ROUTES << 8) | REGIONS)
    value = hash_word(value, (SEGMENTS << 16) | (PROFILES << 8))
    value = hash_word(value, PAYLOAD_SIZE)
    value = hash_word(value, schema)
    value = hash_word(value, generation)
    for region in range(REGIONS):
        for route in range(ROUTES):
            for stat in payload["stats"][region][route]:
                value = hash_word(value, stat)
        for played in payload["played"][region]:
            value = hash_word(value, played)
        for best in payload["best"][region]:
            value = hash_word(value, best)
        for profile in range(PROFILES):
            for identity in payload["identity"][region][profile]:
                value = hash_word(value, identity)
            for pb in payload["pb"][region][profile]:
                value = hash_word(value, pb)
    return value


def pack_file(payload: dict[str, list], generation: int = 1,
              schema: int = SCHEMA_HASH) -> bytes:
    digest = checksum(payload, generation, schema)
    header = FILE_HEADER.pack(
        MAGIC,
        VERSION,
        ROUTES,
        REGIONS,
        SEGMENTS,
        PROFILES,
        0,
        PAYLOAD_SIZE,
        schema,
        generation,
        digest,
        bytes(4),
    )
    body = struct.pack(">" + "I" * (PAYLOAD_SIZE // 4), *payload_words(payload))
    raw = header + body + bytes(FILE_SIZE - FILE_HEADER.size - PAYLOAD_SIZE)
    assert len(raw) == FILE_SIZE
    return raw


def valid_payload(payload: dict[str, list]) -> bool:
    def valid_qf(value: int) -> bool:
        return value == UNSET or 0 <= value <= MAX_QF

    for region in range(REGIONS):
        for attempts, finishes, _golds in payload["stats"][region]:
            if finishes > attempts:
                return False
        if not all(valid_qf(value) for value in payload["best"][region]):
            return False
        for profile in range(PROFILES):
            for route in range(ROUTES):
                identity = payload["identity"][region][profile][route]
                if not valid_qf(identity):
                    return False
                first = ROUTE_FIRST[route]
                end = first + ROUTE_COUNTS[route]
                for value in payload["pb"][region][profile][first:end]:
                    if not valid_qf(value):
                        return False
    return True


def migrate_v1(v1: dict[str, list]) -> dict[str, list]:
    out = blank_v4_payload()
    for region in range(REGIONS):
        for route in range(V1_ROUTES):
            out["stats"][region][route] = list(v1["stats"][region][route])
            out["stats"][region][route][2] = 0
            kept = 1 if route == 0 else 0
            for local in range(kept):
                out["best"][region][V4_ROUTE_FIRST[route] + local] = (
                    v1["best"][region][V1_ROUTE_FIRST[route] + local]
                )
        for profile in range(PROFILES):
            for route in range(V1_ROUTES):
                kept = 1 if route == 0 else 0
                for local in range(kept):
                    out["pb"][region][profile][V4_ROUTE_FIRST[route] + local] = (
                        v1["pb"][region][profile]
                          [V1_ROUTE_FIRST[route] + local]
                    )
    return migrate_v4(out)


def migrate_pr7(payload: dict[str, list]) -> dict[str, list]:
    out = deepcopy(payload)
    for region in range(REGIONS):
        for route in (4, 5, 6):
            out["stats"][region][route][2] = 0
            first = ROUTE_FIRST[route]
            end = first + ROUTE_COUNTS[route]
            out["best"][region][first:end] = [UNSET] * (end - first)
            for profile in range(PROFILES):
                out["identity"][region][profile][route] = UNSET
                out["pb"][region][profile][first:end] = (
                    [UNSET] * (end - first)
                )
    return out


def migrate_v2(v2: dict[str, list]) -> dict[str, list]:
    out = blank_v4_payload()
    for region in range(REGIONS):
        for route in range(V2_ROUTES):
            out["stats"][region][route] = list(v2["stats"][region][route])
            if route in (1, 2, 5):
                out["stats"][region][route][2] = 0
                continue
            for local in range(V2_ROUTE_COUNTS[route]):
                out["best"][region][V4_ROUTE_FIRST[route] + local] = (
                    v2["best"][region][V2_ROUTE_FIRST[route] + local]
                )
            for profile in range(PROFILES):
                out["identity"][region][profile][route] = (
                    v2["identity"][region][profile][route]
                )
                for local in range(V2_ROUTE_COUNTS[route]):
                    out["pb"][region][profile][V4_ROUTE_FIRST[route] + local] = (
                        v2["pb"][region][profile]
                          [V2_ROUTE_FIRST[route] + local]
                    )
    return migrate_v4(out)


def migrate_v3(v3: dict[str, list]) -> dict[str, list]:
    out = blank_v4_payload()
    for region in range(REGIONS):
        for route in range(V3_ROUTES):
            out["stats"][region][route][:2] = v3["stats"][region][route][:2]
            if route in V3_CHANGED_ROUTES:
                continue
            out["stats"][region][route][2] = v3["stats"][region][route][2]
            for local in range(V3_ROUTE_COUNTS[route]):
                out["best"][region][V4_ROUTE_FIRST[route] + local] = (
                    v3["best"][region][V3_ROUTE_FIRST[route] + local]
                )
            for profile in range(PROFILES):
                out["identity"][region][profile][route] = (
                    v3["identity"][region][profile][route]
                )
                for local in range(V3_ROUTE_COUNTS[route]):
                    out["pb"][region][profile][V4_ROUTE_FIRST[route] + local] = (
                        v3["pb"][region][profile]
                          [V3_ROUTE_FIRST[route] + local]
                    )
    return migrate_v4(out)


def migrate_v4(v4: dict[str, list]) -> dict[str, list]:
    out = blank_payload()
    for region in range(REGIONS):
        for route in range(V5_ROUTES):
            removed = V4_REMOVED_CHECKPOINT.get(route)
            terminal = route in (10, 31)
            out["stats"][region][route][:2] = v4["stats"][region][route][:2]
            if removed is None and not terminal:
                out["stats"][region][route][2] = v4["stats"][region][route][2]
            for local in range(ROUTE_COUNTS[route]):
                if terminal:
                    continue
                if local != removed:
                    old_local = local + (removed is not None and local > removed)
                    out["best"][region][ROUTE_FIRST[route] + local] = (
                        v4["best"][region][V4_ROUTE_FIRST[route] + old_local]
                    )
            for profile in range(PROFILES):
                identity = v4["identity"][region][profile][route]
                out["identity"][region][profile][route] = identity
                if terminal:
                    out["pb"][region][profile][ROUTE_FIRST[route]] = identity
                    continue
                for local in range(ROUTE_COUNTS[route]):
                    if local == removed:
                        old = V4_ROUTE_FIRST[route] + local
                        first = v4["pb"][region][profile][old]
                        second = v4["pb"][region][profile][old + 1]
                        value = (
                            first + second
                            if first != UNSET and second != UNSET
                            and first + second <= MAX_QF
                            else UNSET
                        )
                    else:
                        old_local = local + (
                            removed is not None and local > removed
                        )
                        value = v4["pb"][region][profile][
                            V4_ROUTE_FIRST[route] + old_local
                        ]
                    out["pb"][region][profile][
                        ROUTE_FIRST[route] + local
                    ] = value
    return out


def migrate_v5(v5: dict[str, list]) -> dict[str, list]:
    out = blank_payload()
    for region in range(REGIONS):
        for route in range(V5_ROUTES):
            out["stats"][region][route][:2] = v5["stats"][region][route][:2]
            if route not in (10, 31):
                out["stats"][region][route][2] = v5["stats"][region][route][2]
                for local in range(V5_ROUTE_COUNTS[route]):
                    out["best"][region][ROUTE_FIRST[route] + local] = (
                        v5["best"][region][V5_ROUTE_FIRST[route] + local]
                    )
            for profile in range(PROFILES):
                identity = v5["identity"][region][profile][route]
                out["identity"][region][profile][route] = identity
                if route in (10, 31):
                    if identity != UNSET:
                        out["pb"][region][profile][ROUTE_FIRST[route]] = identity
                    continue
                for local in range(V5_ROUTE_COUNTS[route]):
                    out["pb"][region][profile][ROUTE_FIRST[route] + local] = (
                        v5["pb"][region][profile]
                          [V5_ROUTE_FIRST[route] + local]
                    )
    return out


def select_v2(records: list[tuple[int, int, dict[str, list], bool] | None]
              ) -> tuple[bool, tuple[int, int, dict[str, list], bool] | None]:
    safe = True
    selected = None
    for record in records:
        if record is None:
            continue
        schema, generation, payload, valid = record
        if schema not in (V2_SCHEMA_HASH, PR7_SCHEMA_HASH):
            safe = False
            continue
        if not valid:
            continue
        if selected is None:
            selected = record
            continue
        old_schema, old_generation, old_payload, _old_valid = selected
        if generation == old_generation:
            if payload != old_payload:
                safe = False
            if schema == V2_SCHEMA_HASH and old_schema == PR7_SCHEMA_HASH:
                selected = record
            continue
        if generation_is_newer(generation, old_generation):
            selected = record
    return safe, selected


class SplitContractTests(unittest.TestCase):
    def test_exact_layout_constants(self) -> None:
        text = HEADER.read_text(encoding="utf-8")
        expected = {
            "SUSAMUNE_SPLIT_STATS_SCHEMA_HASH":
                f"0x{SCHEMA_HASH:08X}u",
            "SUSAMUNE_SPLIT_STATS_V6_SCHEMA_HASH": "0xF7EAA0C4u",
            "SUSAMUNE_SPLIT_STATS_V5_SCHEMA_HASH": "0xA91743AAu",
            "SUSAMUNE_SPLIT_STATS_V4_SCHEMA_HASH": "0x2E5CC875u",
            "SUSAMUNE_SPLIT_STATS_V3_SCHEMA_HASH": "0xD452F6FDu",
            "SUSAMUNE_SPLIT_STATS_PR7_SCHEMA_HASH": "0xB9B6E310u",
            "SUSAMUNE_SPLIT_STATS_CFG_OFFSET": "0x8800u",
        }
        for name, value in expected.items():
            self.assertRegex(text, rf"#define\s+{name}\s+{value}")
        self.assertIn("SUSAMUNE_SPLIT_STATS_VERSION        7u", text)
        self.assertIn("SUSAMUNE_SPLIT_STATS_ROUTE_COUNT    122u", text)
        self.assertIn("SUSAMUNE_SPLIT_STATS_SEGMENT_COUNT  275u", text)
        self.assertIn("SUSAMUNE_ILING_PB_SLOT_COUNT     126u", text)
        self.assertIn("sizeof(struct SusamuneSplitStatsPayload) == 0x6E34", text)
        self.assertIn("sizeof(struct SusamuneSplitStatsCfg) == 0x7000", text)
        self.assertIn("sizeof(struct SusamuneSplitStatsFile) == 0x6FE0", text)
        self.assertIn("sizeof(struct SusamuneSplitStatsFileV5) == 0x47E0", text)
        self.assertIn("sizeof(struct SusamuneSplitStatsFileV4) == 0x47E0", text)
        self.assertIn("sizeof(struct SusamuneSplitStatsFileV3) == 0x4540", text)
        self.assertIn("sizeof(struct SusamuneSplitStatsFileV2) == 0xC60", text)
        self.assertIn("sizeof(struct SusamuneSplitStatsFileV1) == 0x340", text)
        self.assertEqual(FILE_HEADER.size, 0x20)
        self.assertEqual(MAILBOX_OFFSET + MAILBOX_SIZE, 0xF800)

    def test_split_runtime_is_bss_and_mailbox_partition_is_exact(self) -> None:
        text = MEM2.read_text(encoding="utf-8")
        source = SPLITS.read_text(encoding="utf-8")
        self.assertNotIn("SPLIT_STATS_RUNTIME", text)
        self.assertIn("Runtime sStateStorage;", source)
        self.assertIn("sizeof(Runtime) == 0x6E90", source)
        self.assertEqual(MAILBOX_OFFSET + MAILBOX_SIZE, 0xF800)

    def test_route_indices_match_catalog_labels(self) -> None:
        lines = ILING_ENTRIES.read_text(encoding="utf-8")
        labels = re.findall(r'^\w+\("([^"]+)"', lines, re.MULTILINE)
        for entry, name in zip(ROUTE_ENTRIES, ROUTE_CATALOG_NAMES):
            self.assertEqual(labels[entry], name)
        source = SPLITS.read_text(encoding="utf-8")
        for first, entry, checkpoints in zip(
            ROUTE_FIRST, ROUTE_ENTRIES, ROUTE_CHECKPOINTS
        ):
            self.assertIn(f"{{{first}, {entry}, {checkpoints}}}", source)

    def test_append_only_public_route_ids(self) -> None:
        text = SPLIT_API.read_text(encoding="utf-8")
        self.assertRegex(text, r"ROUTE_BIANCO_4\s*=\s*0")
        self.assertRegex(text, r"ROUTE_GELATO_8_GBS\s*=\s*1")
        self.assertRegex(text, r"ROUTE_PIANTA_6\s*=\s*2")
        for episode in range(1, 8):
            self.assertRegex(text, rf"ROUTE_RICCO_{episode}\s*=\s*{episode + 2}")
        self.assertRegex(text, r"ROUTE_BOWSER\s*=\s*58")
        self.assertRegex(text, r"ROUTE_RICCO_2_RACE\s*=\s*59")
        self.assertRegex(text, r"ROUTE_RICCO_4_SECRET\s*=\s*60")
        self.assertRegex(text, r"ROUTE_COUNT\s*=\s*122")

    def test_gbs_uses_credited_route_only_for_stage_loader_alias(self) -> None:
        source = SPLITS.read_text(encoding="utf-8")
        self.assertIn(
            "entry == 25 && StageLoader::activeRouteMatches(25, 121)",
            source,
        )
        self.assertIn("{4, 121, 2}", source)
        self.assertIn("entry == 121", source)
        self.assertNotIn("entry == 35 || entry == 121", source)
        self.assertIn("entry == 0 && StageLoader::activeRouteMatches(0, 1)", source)
        self.assertIn("sState->activeRoute == SplitStats::ROUTE_BIANCO_2 && entry == 0", source)
        self.assertIn("return routeForEntry(entry);", source)
        self.assertIn("sState->activeRoute != routeIndex", source)
        self.assertIn("completedPb < identity", source)
        self.assertIn("const bool updatePb = complete", source)
        self.assertNotIn("pbAccepted", source)
        self.assertIn("ILing::label(kRoutes[route].entry)", source)
        menu = MENU.read_text(encoding="utf-8")
        self.assertIn("SplitStats::summary(mStatsEntry, &stats)", menu)

    def test_every_route_bootstraps_an_internal_profile_pb(self) -> None:
        unset = 0xFFFFFFFF

        def update(current: int, qf: int, complete: bool, eligible: bool) -> int:
            if complete and eligible and (current == unset or qf < current):
                return qf
            return current

        self.assertEqual(update(unset, 1200, True, True), 1200)
        self.assertEqual(update(1200, 1300, True, True), 1200)
        self.assertEqual(update(1200, 1100, False, True), 1200)
        self.assertEqual(update(1200, 1100, True, False), 1200)
        self.assertEqual(update(1200, 1100, True, True), 1100)

    def test_attempt_identity_survives_attempt_end_for_dedupe(self) -> None:
        source = SPLITS.read_text(encoding="utf-8")
        end = source[source.index("void endAttempt()") :]
        end = end[: end.index("void formatDelta")]
        self.assertNotIn("lastCountedRoute = 0xff", end)

    def test_schema_hash_golden_vector(self) -> None:
        self.assertEqual(checkpoint_schema.ROUTE_ENTRIES, ROUTE_ENTRIES)
        self.assertEqual(
            tuple(map(len, checkpoint_schema.CHECKPOINTS)),
            ROUTE_CHECKPOINTS,
        )
        self.assertEqual(schema_hash(), SCHEMA_HASH)

    def test_file_round_trip_shape_and_checksum(self) -> None:
        payload = blank_payload()
        payload["stats"][0][0] = [14, 5, 3]
        payload["best"][0][0:3] = [120, 240, 360]
        payload["identity"][0][0][0] = 1024
        payload["pb"][0][0][0:3] = [128, 256, 384]
        self.assertTrue(valid_payload(payload))
        raw = pack_file(payload, generation=0xFFFFFFFE)
        self.assertEqual(len(raw), FILE_SIZE)
        fields = FILE_HEADER.unpack_from(raw)
        self.assertEqual(fields[0], MAGIC)
        self.assertEqual(fields[9], 0xFFFFFFFE)
        self.assertEqual(fields[10], checksum(payload, 0xFFFFFFFE))

    def test_v5_header_carries_wide_segment_count_and_schema(self) -> None:
        payload = blank_payload()
        raw = pack_file(payload, generation=17)
        fields = FILE_HEADER.unpack_from(raw)
        self.assertEqual(fields[1:7],
                         (VERSION, ROUTES, REGIONS, SEGMENTS, PROFILES, 0))
        self.assertEqual(fields[7], PAYLOAD_SIZE)
        self.assertEqual(fields[8], SCHEMA_HASH)
        self.assertEqual(fields[9], 17)
        self.assertEqual(fields[10], checksum(payload, 17))

    def test_v2_selection_spans_current_and_pr7_schemas(self) -> None:
        legacy = blank_v2_payload()
        current = blank_v2_payload()
        legacy["stats"][0][0] = [8, 3, 1]
        current["stats"][0][0] = [9, 4, 2]

        safe, selected = select_v2([
            (V2_SCHEMA_HASH, 8, current, True),
            (PR7_SCHEMA_HASH, 9, legacy, True),
        ])
        self.assertTrue(safe)
        self.assertEqual(selected[:2], (PR7_SCHEMA_HASH, 9))

        safe, selected = select_v2([
            (PR7_SCHEMA_HASH, 0xFFFFFFFF, legacy, True),
            (V2_SCHEMA_HASH, 0, current, True),
        ])
        self.assertTrue(safe)
        self.assertEqual(selected[:2], (V2_SCHEMA_HASH, 0))

    def test_v2_equal_generation_conflict_is_read_only(self) -> None:
        legacy = blank_v2_payload()
        current = blank_v2_payload()
        current["stats"][0][0] = [1, 0, 0]
        safe, selected = select_v2([
            (PR7_SCHEMA_HASH, 12, legacy, True),
            (V2_SCHEMA_HASH, 12, current, True),
        ])
        self.assertFalse(safe)
        self.assertEqual(selected[:2], (V2_SCHEMA_HASH, 12))

        safe, selected = select_v2([
            (PR7_SCHEMA_HASH, 12, legacy, True),
            (V2_SCHEMA_HASH, 12, legacy, True),
        ])
        self.assertTrue(safe)
        self.assertEqual(selected[:2], (V2_SCHEMA_HASH, 12))

    def test_v2_torn_known_record_is_ignored_but_unknown_hash_is_unsafe(self) -> None:
        payload = blank_v2_payload()
        safe, selected = select_v2([
            (PR7_SCHEMA_HASH, 4, payload, False),
            (V2_SCHEMA_HASH, 3, payload, True),
        ])
        self.assertTrue(safe)
        self.assertEqual(selected[:2], (V2_SCHEMA_HASH, 3))

        safe, selected = select_v2([
            (V2_SCHEMA_HASH ^ 1, 5, payload, True),
            (V2_SCHEMA_HASH, 3, payload, True),
        ])
        self.assertFalse(safe)
        self.assertEqual(selected[:2], (V2_SCHEMA_HASH, 3))

    def test_pr7_migration_resets_only_changed_ricco_routes(self) -> None:
        payload = blank_v2_payload()
        for region in range(REGIONS):
            for route in range(V2_ROUTES):
                payload["stats"][region][route] = [20 + route, 10, 7]
                first = ROUTE_FIRST[route]
                end = first + ROUTE_COUNTS[route]
                payload["best"][region][first:end] = [
                    1000 + region * 100 + segment
                    for segment in range(first, end)
                ]
                for profile in range(PROFILES):
                    payload["identity"][region][profile][route] = (
                        2000 + region * 100 + profile * 10 + route
                    )
                    payload["pb"][region][profile][first:end] = [
                        3000 + region * 100 + profile * 10 + segment
                        for segment in range(first, end)
                    ]

        migrated = migrate_pr7(payload)
        for region in range(REGIONS):
            for route in range(V2_ROUTES):
                first = ROUTE_FIRST[route]
                end = first + ROUTE_COUNTS[route]
                if route in (4, 5, 6):
                    self.assertEqual(
                        migrated["stats"][region][route][:2],
                        payload["stats"][region][route][:2],
                    )
                    self.assertEqual(migrated["stats"][region][route][2], 0)
                    self.assertEqual(
                        migrated["best"][region][first:end],
                        [UNSET] * (end - first),
                    )
                    for profile in range(PROFILES):
                        self.assertEqual(
                            migrated["identity"][region][profile][route],
                            UNSET,
                        )
                        self.assertEqual(
                            migrated["pb"][region][profile][first:end],
                            [UNSET] * (end - first),
                        )
                else:
                    self.assertEqual(
                        migrated["stats"][region][route],
                        payload["stats"][region][route],
                    )
                    self.assertEqual(
                        migrated["best"][region][first:end],
                        payload["best"][region][first:end],
                    )
                    for profile in range(PROFILES):
                        self.assertEqual(
                            migrated["identity"][region][profile][route],
                            payload["identity"][region][profile][route],
                        )
                        self.assertEqual(
                            migrated["pb"][region][profile][first:end],
                            payload["pb"][region][profile][first:end],
                        )

    def test_v2_migration_preserves_history_but_resets_changed_routes(self) -> None:
        payload = blank_v2_payload()
        for region in range(REGIONS):
            for route in range(V2_ROUTES):
                payload["stats"][region][route] = [20 + route, 10, 7]
                first = V2_ROUTE_FIRST[route]
                end = first + V2_ROUTE_COUNTS[route]
                payload["best"][region][first:end] = [
                    1000 + segment for segment in range(first, end)
                ]
                for profile in range(PROFILES):
                    payload["identity"][region][profile][route] = 2000 + route
                    payload["pb"][region][profile][first:end] = [
                        3000 + segment for segment in range(first, end)
                    ]

        migrated = migrate_v2(payload)
        for region in range(REGIONS):
            for route in range(V2_ROUTES):
                first = ROUTE_FIRST[route]
                end = first + ROUTE_COUNTS[route]
                self.assertEqual(migrated["stats"][region][route][:2],
                                 payload["stats"][region][route][:2])
                if route in (1, 2, 5):
                    self.assertEqual(migrated["stats"][region][route][2], 0)
                    self.assertEqual(migrated["best"][region][first:end],
                                     [UNSET] * (end - first))
                    for profile in range(PROFILES):
                        self.assertEqual(
                            migrated["identity"][region][profile][route], UNSET
                        )
                        self.assertEqual(
                            migrated["pb"][region][profile][first:end],
                            [UNSET] * (end - first),
                        )
                else:
                    self.assertEqual(migrated["stats"][region][route],
                                     payload["stats"][region][route])
                    self.assertEqual(migrated["best"][region][first:end],
                                     payload["best"][region][first:end])
                    for profile in range(PROFILES):
                        self.assertEqual(
                            migrated["identity"][region][profile][route],
                            payload["identity"][region][profile][route],
                        )
                        self.assertEqual(
                            migrated["pb"][region][profile][first:end],
                            payload["pb"][region][profile][first:end],
                        )
            for route in range(V2_ROUTES, ROUTES):
                self.assertEqual(migrated["stats"][region][route], [0, 0, 0])

    def test_v3_migration_is_exhaustively_route_local(self) -> None:
        payload = blank_v3_payload()
        for region in range(REGIONS):
            for route in range(V3_ROUTES):
                payload["stats"][region][route] = [100 + route, 50, 9]
                for local in range(V3_ROUTE_COUNTS[route]):
                    old = V3_ROUTE_FIRST[route] + local
                    payload["best"][region][old] = 1000 + old
                for profile in range(PROFILES):
                    payload["identity"][region][profile][route] = (
                        2000 + profile * 100 + route
                    )
                    for local in range(V3_ROUTE_COUNTS[route]):
                        old = V3_ROUTE_FIRST[route] + local
                        payload["pb"][region][profile][old] = (
                            3000 + profile * 1000 + old
                        )

        migrated = migrate_v3(payload)
        self.assertTrue(valid_payload(migrated))
        for region in range(REGIONS):
            for route in range(V3_ROUTES):
                self.assertEqual(
                    migrated["stats"][region][route][:2],
                    payload["stats"][region][route][:2],
                )
                first = ROUTE_FIRST[route]
                end = first + ROUTE_COUNTS[route]
                if route in V3_CHANGED_ROUTES:
                    self.assertEqual(migrated["stats"][region][route][2], 0)
                    self.assertEqual(
                        migrated["best"][region][first:end],
                        [UNSET] * ROUTE_COUNTS[route],
                    )
                    for profile in range(PROFILES):
                        self.assertEqual(
                            migrated["identity"][region][profile][route],
                            UNSET,
                        )
                        self.assertEqual(
                            migrated["pb"][region][profile][first:end],
                            [UNSET] * ROUTE_COUNTS[route],
                        )
                    continue

                if route in (10, 31):
                    self.assertEqual(migrated["stats"][region][route][2], 0)
                    self.assertEqual(migrated["best"][region][first], UNSET)
                    for profile in range(PROFILES):
                        identity = payload["identity"][region][profile][route]
                        self.assertEqual(
                            migrated["identity"][region][profile][route], identity
                        )
                        self.assertEqual(
                            migrated["pb"][region][profile][first], identity
                        )
                    continue

                if route in V4_REMOVED_CHECKPOINT:
                    removed = V4_REMOVED_CHECKPOINT[route]
                    self.assertEqual(migrated["stats"][region][route][2], 0)
                    for local in range(ROUTE_COUNTS[route]):
                        new = ROUTE_FIRST[route] + local
                        if local == removed:
                            self.assertEqual(migrated["best"][region][new], UNSET)
                        else:
                            old_local = local + (local > removed)
                            old = V3_ROUTE_FIRST[route] + old_local
                            self.assertEqual(
                                migrated["best"][region][new],
                                payload["best"][region][old],
                            )
                        for profile in range(PROFILES):
                            if local == removed:
                                old = V3_ROUTE_FIRST[route] + removed
                                expected = (
                                    payload["pb"][region][profile][old]
                                    + payload["pb"][region][profile][old + 1]
                                )
                            else:
                                old_local = local + (local > removed)
                                expected = payload["pb"][region][profile][
                                    V3_ROUTE_FIRST[route] + old_local
                                ]
                            self.assertEqual(
                                migrated["pb"][region][profile][new], expected
                            )
                    continue

                self.assertEqual(
                    migrated["stats"][region][route],
                    payload["stats"][region][route],
                )
                for local in range(V3_ROUTE_COUNTS[route]):
                    old = V3_ROUTE_FIRST[route] + local
                    new = ROUTE_FIRST[route] + local
                    self.assertEqual(
                        migrated["best"][region][new],
                        payload["best"][region][old],
                    )
                    for profile in range(PROFILES):
                        self.assertEqual(
                            migrated["pb"][region][profile][new],
                            payload["pb"][region][profile][old],
                        )
                for profile in range(PROFILES):
                    self.assertEqual(
                        migrated["identity"][region][profile][route],
                        payload["identity"][region][profile][route],
                    )
            for route in range(V3_ROUTES, ROUTES):
                self.assertEqual(migrated["stats"][region][route], [0, 0, 0])
                self.assertEqual(
                    migrated["best"][region]
                            [ROUTE_FIRST[route]:
                             ROUTE_FIRST[route] + ROUTE_COUNTS[route]],
                    [UNSET] * ROUTE_COUNTS[route],
                )

        # Pinna 4 moves left by one segment; a flat-prefix migration fails this.
        self.assertNotEqual(V3_ROUTE_FIRST[35], ROUTE_FIRST[35])
        self.assertEqual(
            migrated["best"][0][ROUTE_FIRST[35]],
            payload["best"][0][V3_ROUTE_FIRST[35]],
        )

    def test_v4_migration_drops_only_removed_golds_and_merges_pb_segments(self) -> None:
        payload = blank_v4_payload()
        for route, removed in V4_REMOVED_CHECKPOINT.items():
            payload["stats"][0][route] = [12, 7, 5]
            payload["identity"][0][0][route] = 9000 + route
            for local in range(V4_ROUTE_COUNTS[route]):
                old = V4_ROUTE_FIRST[route] + local
                payload["best"][0][old] = 100 + local
                payload["pb"][0][0][old] = 200 + local

        migrated = migrate_v4(payload)
        for route, removed in V4_REMOVED_CHECKPOINT.items():
            self.assertEqual(migrated["stats"][0][route], [12, 7, 0])
            self.assertEqual(migrated["identity"][0][0][route], 9000 + route)
            for local in range(ROUTE_COUNTS[route]):
                new = ROUTE_FIRST[route] + local
                if local == removed:
                    self.assertEqual(migrated["best"][0][new], UNSET)
                    self.assertEqual(
                        migrated["pb"][0][0][new], 401 + 2 * removed
                    )
                else:
                    old_local = local + (local > removed)
                    self.assertEqual(
                        migrated["best"][0][new], 100 + old_local
                    )
                    self.assertEqual(
                        migrated["pb"][0][0][new], 200 + old_local
                    )

    def test_payload_allows_migrated_partial_pb_but_rejects_bad_counts(self) -> None:
        payload = blank_payload()
        payload["identity"][1][2][1] = 500
        payload["pb"][1][2][3] = 100
        self.assertTrue(valid_payload(payload))
        payload = blank_payload()
        payload["stats"][2][2] = [4, 5, 0]
        self.assertFalse(valid_payload(payload))

    def test_v1_migration_invalidates_changed_b4_and_unsets_finals(self) -> None:
        v1 = {
            "stats": [[[0, 0, 0] for _ in range(V1_ROUTES)]
                      for _ in range(REGIONS)],
            "best": [[UNSET] * V1_SEGMENTS for _ in range(REGIONS)],
            "identity": [[[UNSET] * V1_ROUTES for _ in range(PROFILES)]
                         for _ in range(REGIONS)],
            "pb": [[[UNSET] * V1_SEGMENTS for _ in range(PROFILES)]
                   for _ in range(REGIONS)],
        }
        for route in range(V1_ROUTES):
            v1["stats"][2][route] = [10 + route, 5 + route, route]
            for local in range(V1_ROUTE_COUNTS[route]):
                old = V1_ROUTE_FIRST[route] + local
                v1["best"][2][old] = 100 + old
                v1["pb"][2][3][old] = 200 + old
            v1["identity"][2][3][route] = 1000 + route
        out = migrate_v1(v1)
        self.assertTrue(valid_payload(out))
        for route in range(V1_ROUTES):
            expected_stats = list(v1["stats"][2][route])
            expected_stats[2] = 0
            self.assertEqual(out["stats"][2][route], expected_stats)
            kept = 1 if route == 0 else 0
            for local in range(kept):
                old = V1_ROUTE_FIRST[route] + local
                new = ROUTE_FIRST[route] + local
                self.assertEqual(out["best"][2][new], 100 + old)
                self.assertEqual(out["pb"][2][3][new], 200 + old)
            for local in range(kept, V1_ROUTE_COUNTS[route]):
                new = ROUTE_FIRST[route] + local
                self.assertEqual(out["best"][2][new], UNSET)
                self.assertEqual(out["pb"][2][3][new], UNSET)
            self.assertEqual(out["identity"][2][3][route], UNSET)
            final = ROUTE_FIRST[route] + ROUTE_COUNTS[route] - 1
            self.assertEqual(out["best"][2][final], UNSET)
            self.assertEqual(out["pb"][2][3][final], UNSET)
        for route in range(V1_ROUTES, ROUTES):
            self.assertEqual(out["stats"][2][route], [0, 0, 0])

    def test_generation_half_range(self) -> None:
        self.assertTrue(generation_is_newer(0, 0xFFFFFFFF))
        self.assertFalse(generation_is_newer(0xFFFFFFFF, 0))
        self.assertFalse(generation_is_newer(7, 7))
        self.assertFalse(generation_is_newer(0x80000000, 0))

    def test_terminal_segment_completes_sum_of_best(self) -> None:
        self.assertEqual(sum(ROUTE_COUNTS), SEGMENTS)
        source = SPLITS.read_text(encoding="utf-8")
        result = source[source.index("void onILResult(") :]
        result = result[: result.index("void onPBDeleted")]
        terminal = result.index(
            "captureSegment(route, desc.checkpointCount, qf);"
        )
        commit = result.index("commitAttemptGolds();")
        self.assertLess(terminal, commit)
        summary = source[source.index("bool summary(") :]
        self.assertIn("local < out->segmentCount", summary)
        bests = [100, 200, 300, UNSET]
        self.assertIsNone(None if UNSET in bests else sum(bests))
        bests[-1] = 400
        self.assertEqual(sum(bests), 1000)

    def test_livesplit_delta_and_gold_color_precedence(self) -> None:
        def color(elapsed: int, duration: int, best: int,
                  pb_elapsed: int) -> tuple[str, int]:
            delta = elapsed - pb_elapsed
            if best == UNSET or duration < best:
                return "gold", delta
            if delta < 0:
                return "green", delta
            if delta == 0:
                return "white", delta
            return "red", delta

        self.assertEqual(color(90, 90, 100, 95), ("gold", -5))
        # Ten QFs saved on split 1, then three lost on split 2: still -7.
        self.assertEqual(color(193, 103, 90, 200), ("green", -7))
        self.assertEqual(color(200, 110, 90, 200), ("white", 0))
        self.assertEqual(color(201, 111, 90, 200), ("red", 1))
        source = SPLITS.read_text(encoding="utf-8")
        self.assertIn("prior <= local", source)
        self.assertIn("delta = absoluteQf - (s32)pbElapsed;", source)
        self.assertIn("color != OVERLAY_GOLD", source)

    def test_stats_has_nested_variable_segment_details(self) -> None:
        api = SPLIT_API.read_text(encoding="utf-8")
        source = SPLITS.read_text(encoding="utf-8")
        menu = MENU.read_text(encoding="utf-8")
        self.assertIn("s32 pbSplitQf;", api)
        self.assertIn("s32 pbSegmentQf;", api)
        self.assertIn("s32 goldQf;", api)
        self.assertIn("u8 segmentCount;", api)
        self.assertIn("out->segmentCount = segmentCount(desc);", source)
        self.assertIn("pbSplitQf += item.pbSegmentQf;", source)
        self.assertIn("mShowingSegments = true;", menu)
        self.assertIn('"PB SPLIT (SEGMENT) / GOLD"', menu)
        self.assertIn('"%s (%ss)   Gold: %s"', menu)
        self.assertIn("i < stats.segmentCount", menu)
        self.assertNotIn("Coming in a later V2.1 pre-release", menu)

    def test_qft_duration_source_contract(self) -> None:
        qft = QFT.read_text(encoding="utf-8")
        splits = SPLITS.read_text(encoding="utf-8")
        pattern = r"kFreezeFrames\[\]\s*=\s*\{([^}]+)\}"
        qft_values = tuple(map(int, re.search(pattern, qft).group(1).split(",")))
        split_values = tuple(
            map(int, re.search(pattern, splits).group(1).split(","))
        )
        self.assertEqual(qft_values, FREEZE_FRAMES)
        self.assertEqual(split_values, qft_values)

    def test_capture_always_arms_its_own_exact_snapshot(self) -> None:
        source = SPLITS.read_text(encoding="utf-8")
        event = source[source.index("bool captureSegment(") :]
        capture = event.index("sState->attemptQf[local] = duration;")
        presentation = event.index("armOverlay(color, havePb, delta, absoluteQf);")
        self.assertLess(capture, presentation)
        self.assertNotIn("overlayEventEnabled", source)

    def test_actual_qft_render_handshake_order(self) -> None:
        menu = MENU.read_text(encoding="utf-8")
        splits = SPLITS.read_text(encoding="utf-8")
        begin = menu.index("gQftDisplay.beginOverlayFrame();")
        qft = menu.index("gQFTTimer.draw(this);", begin)
        split = menu.index("SplitStats::draw(this);", qft)
        self.assertLess(begin, qft)
        self.assertLess(qft, split)
        display = QFT_DISPLAY.read_text(encoding="utf-8")
        self.assertIn("sAnchorDrawn = true;", display)
        self.assertIn("strcmp(sAnchorText, text) == 0", display)
        self.assertIn("formatAnchorQf(sState->overlayAnchorQf", splits)
        self.assertIn(
            "gQftDisplay.adjacentStyle(anchor, sState->overlayText, &style)",
            splits,
        )
        self.assertIn("if (gQftDisplay.hasAnchor(anchor)) return;", splits)
        self.assertIn("gQftDisplay.draw(menu, anchor);", splits)
        self.assertIn("const int anchorRight", display)
        self.assertIn("const int rightX = anchorRight + 10 + pad;", display)
        self.assertIn("const int available = 640 - pad - rightX;", display)
        self.assertIn("out->scale = (u8)(size * 5);", display)
        self.assertNotIn("leftX", display)
        creation = CREATION.read_text(encoding="utf-8")
        self.assertIn("return splitTextWidth(text, size, &count);", creation)

    def test_split_display_toggle_and_jp_plus_are_presentation_only(self) -> None:
        source = SPLITS.read_text(encoding="utf-8")
        draw = source[source.index("void draw(Menu *menu)") :]
        self.assertIn(
            "!gSettings.getBool(SETTING_LEVEL_SPLITS)", draw
        )
        capture = source[
            source.index("bool captureSegment("):
            source.index("bool validMailbox(")
        ]
        self.assertNotIn("SETTING_LEVEL_SPLITS", capture)
        self.assertIn("const bool customPlus = sState->overlayText[0] == '+';", draw)
        self.assertIn("layoutText[0] = 'P';", draw)
        self.assertIn("drawJpPositiveDelta", draw)
        self.assertIn("menu->fillBox(cx - arm / 2", source)
        self.assertIn("menu->fillBox(cx - stroke / 2", source)

    def test_delta_geometry_stays_strictly_right_or_suppresses(self) -> None:
        def place(anchor_x: int, anchor_width: int, pad: int,
                  delta_widths: dict[int, int], size: int) -> tuple[int, int] | None:
            anchor_right = anchor_x + anchor_width + pad
            right_x = anchor_right + 10 + pad
            available = 640 - pad - right_x
            while size >= 10 and delta_widths[size] > available:
                size -= 1
            return None if size < 10 else (right_x, size)

        widths = {size: size * 4 for size in range(10, 41)}
        normal = place(16, 92, 2, widths, 20)
        self.assertEqual(normal, (122, 20))
        self.assertGreaterEqual(normal[0] - 2, 16 + 92 + 2 + 10)
        squeezed = place(500, 60, 2, widths, 20)
        self.assertEqual(squeezed, (574, 16))
        self.assertLessEqual(squeezed[0] + widths[squeezed[1]] + 2, 640)
        self.assertIsNone(place(610, 20, 2, widths, 20))

    def test_kernel_uses_atomic_ab_and_serial_service(self) -> None:
        source = KERNEL.read_text(encoding="utf-8")
        self.assertIn("susamune_il_stats_v7_a.bin", source)
        self.assertIn("susamune_il_stats_v7_b.bin", source)
        self.assertIn("susamune_il_stats_v6_a.bin", source)
        self.assertIn("susamune_il_stats_v6_b.bin", source)
        self.assertIn("susamune_il_stats_v5_a.bin", source)
        self.assertIn("susamune_il_stats_v5_b.bin", source)
        self.assertIn("susamune_il_stats_v4_a.bin", source)
        self.assertIn("susamune_il_stats_v4_b.bin", source)
        self.assertIn("susamune_il_stats_v3_a.bin", source)
        self.assertIn("susamune_il_stats_v3_b.bin", source)
        self.assertIn("susamune_il_stats_v2_a.bin", source)
        self.assertIn("susamune_il_stats_v2_b.bin", source)
        self.assertIn("susamune_il_stats_v1_a.bin", source)
        self.assertIn("susamune_il_stats_v1_b.bin", source)
        self.assertIn("ReadSplitStatsV3File", source)
        self.assertIn("MigrateSplitStatsV3", source)
        self.assertIn("ReadSplitStatsV6File", source)
        self.assertIn("MigrateSplitStatsV6", source)
        self.assertIn("ReadSplitStatsV1File", source)
        self.assertIn("MigrateSplitStatsV1", source)
        self.assertIn("SUSAMUNE_SPLIT_STATS_FLAG_MIGRATED", source)
        v5_read = source[source.index("static enum PbReadResult ReadSplitStatsFile") :]
        v5_read = v5_read[:v5_read.index("static bool SplitStatsV4PayloadValid")]
        self.assertGreaterEqual(v5_read.count("return PB_READ_UNSAFE;"), 4)
        self.assertIn("struct SusamuneSplitStatsFile, generation", v5_read)
        self.assertIn("file->checksum != SplitStatsChecksum(file)", v5_read)
        self.assertIn("file->schemaHash != SUSAMUNE_SPLIT_STATS_SCHEMA_HASH",
                      v5_read)
        v4_read = source[source.index("static enum PbReadResult ReadSplitStatsV4File") :]
        v4_read = v4_read[:v4_read.index("static bool SplitStatsV4RemovedCheckpoint")]
        self.assertGreaterEqual(v4_read.count("return PB_READ_UNSAFE;"), 4)
        self.assertIn("SUSAMUNE_SPLIT_STATS_V4_SCHEMA_HASH", v4_read)
        self.assertIn("file->checksum != SplitStatsV4Checksum(file)", v4_read)
        v3_read = source[source.index("static enum PbReadResult ReadSplitStatsV3File") :]
        v3_read = v3_read[:v3_read.index("static void MigrateSplitStatsV3")]
        self.assertGreaterEqual(v3_read.count("return PB_READ_UNSAFE;"), 4)
        self.assertIn("SUSAMUNE_SPLIT_STATS_V3_SCHEMA_HASH", v3_read)
        self.assertIn("file->checksum != SplitStatsV3Checksum(file)", v3_read)
        migrate_v3_source = source[source.index("static void MigrateSplitStatsV3") :]
        migrate_v3_source = migrate_v3_source[
            :migrate_v3_source.index("static bool SplitStatsV2PayloadValid")
        ]
        self.assertIn(
            "changedRoutes[] = {5, 34, 36, 37, 46, 48, 49, 57}",
            migrate_v3_source,
        )
        self.assertIn("SplitV4RouteFirst[route] + local", migrate_v3_source)
        self.assertIn("SplitV3RouteFirst[route] + local", migrate_v3_source)
        v2_read = source[source.index("static enum PbReadResult ReadSplitStatsV2File") :]
        v2_read = v2_read[:v2_read.index("static void MigrateSplitStatsPr7")]
        self.assertGreaterEqual(v2_read.count("return PB_READ_UNSAFE;"), 4)
        self.assertIn("SUSAMUNE_SPLIT_STATS_PR7_SCHEMA_HASH", v2_read)
        self.assertIn("file->checksum != SplitStatsV2Checksum(file)", v2_read)
        migrate_pr7 = source[source.index("static void MigrateSplitStatsPr7") :]
        migrate_pr7 = migrate_pr7[:migrate_pr7.index(
            "static bool SplitStatsV1PayloadValid"
        )]
        self.assertIn("changedRoutes[] = {4, 5, 6}", migrate_pr7)
        self.assertIn("routeStats[region][route].golds = 0", migrate_pr7)
        self.assertIn("pbIdentityQf[region][profile][route]", migrate_pr7)
        init_v2 = source[source.index("static bool InitSplitStatsFiles") :]
        init_v2 = init_v2[:init_v2.index("static int WriteSplitStatsFile")]
        self.assertIn("selectedSchemaHash", init_v2)
        self.assertIn("MigrateSplitStatsPr7(&SplitStatsV2Selected);", init_v2)
        self.assertIn(
            "MigrateSplitStatsV2(v4Payload, &SplitStatsV2Selected);",
            init_v2,
        )
        self.assertIn(
            "MigrateSplitStatsV3(v4Payload, selectedPayload);",
            init_v2,
        )
        self.assertIn(
            "MigrateSplitStatsV4(&stats->payload, v4Payload);", init_v2
        )
        self.assertIn("SplitStatsGeneration = selectedGeneration;", init_v2)
        v1_read = source[source.index("static enum PbReadResult ReadSplitStatsV1File") :]
        v1_read = v1_read[:v1_read.index("static void MigrateSplitStatsV1")]
        self.assertGreaterEqual(v1_read.count("return PB_READ_UNSAFE;"), 4)
        write = source.index("static int WriteSplitStatsFile")
        sync = source.index("f_sync(&f)", write)
        close = source.index("f_close(&f)", sync)
        publish = source.index("SplitStatsGeneration = file->generation", close)
        self.assertLess(write, sync)
        self.assertLess(sync, close)
        self.assertLess(close, publish)
        playlist = source.index("if (StagePlaylistSavePending(playlists))")
        splits = source.index("if (!SplitStatsSavePending(splitStats))", playlist)
        self.assertLess(playlist, splits)

    def test_individual_gold_delete_is_transactional_and_keeps_lifetime_count(self) -> None:
        source = SPLITS.read_text(encoding="utf-8")
        api = SPLIT_API.read_text(encoding="utf-8")
        menu = MENU.read_text(encoding="utf-8")
        self.assertIn("DeleteGoldResult deleteGold(int entry, u8 localSegment);",
                      api)
        delete = source[source.index("DeleteGoldResult deleteGold(") :]
        delete = delete[:delete.index("StorageState storageState()")]
        self.assertIn("DELETE_GOLD_READ_ONLY", delete)
        self.assertIn("SUSAMUNE_SPLIT_STATS_QF_UNSET", delete)
        self.assertIn("candidateGoldMask &=", delete)
        self.assertIn("markDirty(true);", delete)
        self.assertNotIn(".golds--", delete)
        self.assertNotIn(".golds =", delete)
        self.assertIn("Delete Segment %d Gold?", menu)
        self.assertIn("SplitStats::deleteGold(mStatsEntry, mStatsSegment)", menu)
        self.assertIn("Gold deleted (session only)", menu)

    def test_savestate_and_fast_forward_invalidate_split_credit(self) -> None:
        actions = ACTION_SOURCE.read_text(encoding="utf-8")
        fast_forward = actions[actions.index("void fastForward(") :]
        fast_forward = fast_forward[:fast_forward.index("}  // namespace")]
        activate = fast_forward.index(
            "if (want != gFfOrig) ILing::invalidateForAssist();"
        )
        patch = fast_forward.index("writeGameCode(kFfSite, want);", activate)
        self.assertLess(activate, patch)

        savestate = SAVESTATE_SOURCE.read_text(encoding="utf-8")
        loaded = savestate.index("gQFTTimer.onSavestateLoaded();")
        split_events = savestate.index(
            "SplitEvents::onSavestateLoaded();", loaded
        )
        split_stats = savestate.index(
            "SplitStats::onSavestateLoaded();", split_events
        )
        iling = savestate.index("ILing::onSavestateLoaded();", split_stats)
        self.assertLess(loaded, split_events)
        self.assertLess(split_events, split_stats)
        self.assertLess(split_stats, iling)

        split_source = SPLITS.read_text(encoding="utf-8")
        restored = split_source[split_source.index("void onSavestateLoaded()") :]
        restored = restored[:restored.index("bool onRouteEvent(")]
        self.assertIn(
            "sState->flags &= ~(FLAG_ATTEMPT_ACTIVE | "
            "FLAG_ATTEMPT_ELIGIBLE |\n                       FLAG_TIME_ACTIVE);",
            restored,
        )
        self.assertIn("commitAttemptTime();", restored)
        self.assertIn("sState->activeRoute = 0xff;", restored)
        self.assertIn("sState->lastCountedRoute = 0xff;", restored)
        self.assertIn(
            "sState->lastAttemptSerial = gQFTTimer.attemptSerial();",
            restored,
        )
        self.assertIn("clearAttemptSamples();", restored)


if __name__ == "__main__":
    unittest.main()
