#!/usr/bin/env python3
"""Focused host contracts for the all-IL V8 split-statistics journal."""

from __future__ import annotations

from dataclasses import dataclass
from itertools import accumulate
import importlib.util
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCHEMA_PATH = ROOT / "scripts" / "split_checkpoint_schema.py"
HEADER = ROOT / "include" / "susamune" / "susamune_cfg.h"
SPLIT_API = ROOT / "include" / "susamune" / "split_stats.hxx"
SPLITS = ROOT / "src" / "split_stats.cpp"
MENU = ROOT / "src" / "menu.cpp"
KERNEL = ROOT / "launcher" / "kernel" / "SusamuneCfg.c"

schema_spec = importlib.util.spec_from_file_location(
    "split_checkpoint_schema_v7", SCHEMA_PATH
)
assert schema_spec and schema_spec.loader
schema = importlib.util.module_from_spec(schema_spec)
schema_spec.loader.exec_module(schema)

VERSION = 8
ROUTES = 132
CHECKPOINTS = 152
SEGMENTS = 285
REGIONS = 3
PROFILES = 4
SCHEMA_HASH = 0xD0AAE2E5
PAYLOAD_SIZE = 0x744C
CFG_SIZE = 0x7500
CFG_OFFSET = 0x8280
UNSET = 0xFFFFFFFF

V5_ROUTES = 61
V5_SEGMENTS = 217
V5_SCHEMA_HASH = 0xA91743AA
V6_ROUTES = 122
V6_SEGMENTS = 274
V6_SCHEMA_HASH = 0xF7EAA0C4
V7_ROUTES = 122
V7_SEGMENTS = 275
V7_SCHEMA_HASH = 0x8ADD6B7D
TERMINAL_ONLY_CHANGED = (10, 31)
B2_ROUTE = 13

ROUTE_ENTRIES = tuple(schema.ROUTE_ENTRIES)
V5_ROUTE_ENTRIES = tuple(schema.V5_ROUTE_ENTRIES)
ROUTE_COUNTS = tuple(len(points) + 1 for points in schema.CHECKPOINTS)
V5_ROUTE_COUNTS = tuple(len(points) + 1 for points in schema.V5_CHECKPOINTS)
ROUTE_FIRST = tuple(
    first + (1 if route > B2_ROUTE else 0)
    for route, first in enumerate((0,) + tuple(accumulate(ROUTE_COUNTS[:-1])))
)
V5_ROUTE_FIRST = (0,) + tuple(accumulate(V5_ROUTE_COUNTS[:-1]))
V6_ROUTE_COUNTS = ROUTE_COUNTS[:V6_ROUTES]
V6_ROUTE_FIRST = (0,) + tuple(accumulate(V6_ROUTE_COUNTS[:-1]))


def source_function(text: str, signature: str, next_signature: str) -> str:
    start = text.index(signature)
    end = text.index(next_signature, start + len(signature))
    return text[start:end]


def function_block(text: str, signature: str) -> str:
    """Return one C/C++ function without depending on declaration order."""
    start = text.index(signature)
    brace = text.index("{", start + len(signature))
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    raise AssertionError(f"unterminated function {signature}")


def parse_int(token: str) -> int:
    return int(token.rstrip("uUlL"), 0)


def macro(text: str, name: str) -> int:
    match = re.search(
        rf"^\s*#define\s+{re.escape(name)}\s+(0[xX][0-9A-Fa-f]+|\d+)[uUlL]*\s*$",
        text,
        re.MULTILINE,
    )
    if not match:
        raise AssertionError(f"missing integer macro {name}")
    return int(match.group(1), 0)


def parse_numeric_array(text: str, name: str) -> tuple[int, ...]:
    match = re.search(
        rf"\b{re.escape(name)}\s*\[[^\]]*\]\s*=\s*\{{(.*?)\}};",
        text,
        re.DOTALL,
    )
    if not match:
        raise AssertionError(f"missing array {name}")
    body = re.sub(r"/\*.*?\*/|//[^\n]*", "", match.group(1), flags=re.DOTALL)
    return tuple(
        parse_int(token)
        for token in re.findall(r"0[xX][0-9A-Fa-f]+[uUlL]*|\d+[uUlL]*", body)
    )


def parse_route_table(text: str) -> tuple[tuple[int, int, int], ...]:
    match = re.search(
        r"const\s+RouteDesc\s+kRoutes\s*\[[^\]]+\]\s*=\s*\{(.*?)\n\};",
        text,
        re.DOTALL,
    )
    if not match:
        raise AssertionError("missing kRoutes")
    return tuple(
        tuple(map(int, row))
        for row in re.findall(
            r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}",
            match.group(1),
        )
    )


def blank_payload(routes: int, segments: int, *, played: bool) -> dict[str, list]:
    payload: dict[str, list] = {
        "stats": [[[0, 0, 0] for _ in range(routes)] for _ in range(REGIONS)],
        "best": [[UNSET] * segments for _ in range(REGIONS)],
        "identity": [
            [[UNSET] * routes for _ in range(PROFILES)] for _ in range(REGIONS)
        ],
        "pb": [
            [[UNSET] * segments for _ in range(PROFILES)]
            for _ in range(REGIONS)
        ],
    }
    if played:
        payload["played"] = [[0] * routes for _ in range(REGIONS)]
    return payload


def migrate_v5(old: dict[str, list]) -> dict[str, list]:
    """Executable model of the frozen V5-to-V7 route-local migration."""
    new = blank_payload(ROUTES, SEGMENTS, played=True)
    for region in range(REGIONS):
        for route in range(V5_ROUTES):
            attempts, finishes, golds = old["stats"][region][route]
            new["stats"][region][route] = [
                attempts,
                finishes,
                0 if route in TERMINAL_ONLY_CHANGED or route == B2_ROUTE
                else golds,
            ]
            new_first = ROUTE_FIRST[route]
            old_first = V5_ROUTE_FIRST[route]
            if route == B2_ROUTE:
                new["best"][region][new_first + 1:new_first + 5] = (
                    old["best"][region][old_first + 1:old_first + 5]
                )
            elif route not in TERMINAL_ONLY_CHANGED:
                count = V5_ROUTE_COUNTS[route]
                new["best"][region][new_first:new_first + count] = (
                    old["best"][region][old_first:old_first + count]
                )
            for profile in range(PROFILES):
                identity = old["identity"][region][profile][route]
                if route == B2_ROUTE:
                    continue
                new["identity"][region][profile][route] = identity
                if route in TERMINAL_ONLY_CHANGED:
                    new["pb"][region][profile][new_first] = identity
                else:
                    count = ROUTE_COUNTS[route]
                    new["pb"][region][profile][new_first:new_first + count] = (
                        old["pb"][region][profile]
                           [old_first:old_first + count]
                    )
    return new


def migrate_v6(old: dict[str, list]) -> dict[str, list]:
    """Executable model of the V6-to-V7 B2 checkpoint migration."""
    new = blank_payload(ROUTES, SEGMENTS, played=True)
    for region in range(REGIONS):
        for route in range(V6_ROUTES):
            attempts, finishes, golds = old["stats"][region][route]
            new["stats"][region][route] = [
                attempts,
                finishes,
                0 if route == B2_ROUTE else golds,
            ]
            new["played"][region][route] = old["played"][region][route]
            new_first = ROUTE_FIRST[route]
            old_first = V6_ROUTE_FIRST[route]
            if route == B2_ROUTE:
                new["best"][region][new_first + 1:new_first + 5] = (
                    old["best"][region][old_first + 1:old_first + 5]
                )
            else:
                count = V6_ROUTE_COUNTS[route]
                new["best"][region][new_first:new_first + count] = (
                    old["best"][region][old_first:old_first + count]
                )
            for profile in range(PROFILES):
                if route == B2_ROUTE:
                    continue
                new["identity"][region][profile][route] = (
                    old["identity"][region][profile][route]
                )
                count = V6_ROUTE_COUNTS[route]
                new["pb"][region][profile][new_first:new_first + count] = (
                    old["pb"][region][profile]
                       [old_first:old_first + count]
                )
    return new


def saturating_add(left: int, right: int) -> int:
    return min(UNSET, left + right)


@dataclass
class PlayedTracker:
    """Small model for commit-on-boundary and live-summary behavior."""

    committed: int = 0
    attempt: int = 0
    last_qf: int = 0
    active: bool = False

    def start(self, qf: int) -> None:
        self.attempt = 0
        self.last_qf = qf
        self.active = True

    def sample(self, qf: int) -> None:
        if not self.active or qf < self.last_qf:
            return
        self.attempt = saturating_add(self.attempt, qf - self.last_qf)
        self.last_qf = qf

    def commit(self, qf: int) -> None:
        if not self.active:
            return
        self.sample(qf)
        self.committed = saturating_add(self.committed, self.attempt)
        self.attempt = 0
        self.active = False

    def invalidate(self, qf: int) -> None:
        self.commit(qf)

    def summary(self, qf: int) -> int:
        live = self.attempt
        if self.active and qf >= self.last_qf:
            live = saturating_add(live, qf - self.last_qf)
        return saturating_add(self.committed, live)


class SchemaContracts(unittest.TestCase):
    def test_every_catalog_entry_has_one_stable_route(self) -> None:
        self.assertEqual(len(ROUTE_ENTRIES), ROUTES)
        self.assertEqual(set(ROUTE_ENTRIES), set(range(ROUTES)))
        self.assertEqual(len(set(ROUTE_ENTRIES)), ROUTES)
        self.assertEqual(ROUTE_ENTRIES[:V5_ROUTES], V5_ROUTE_ENTRIES)
        missing = tuple(sorted(set(range(ROUTES)) - set(V5_ROUTE_ENTRIES)))
        self.assertEqual(ROUTE_ENTRIES[V5_ROUTES:], missing)

    def test_schema_counts_hash_and_terminal_only_routes_are_frozen(self) -> None:
        self.assertEqual(schema.EXPECTED_SCHEMA_HASH, SCHEMA_HASH)
        self.assertEqual(schema.schema_hash(), SCHEMA_HASH)
        self.assertEqual(schema.EXPECTED_V5_SCHEMA_HASH, V5_SCHEMA_HASH)
        self.assertEqual(schema.v5_schema_hash(), V5_SCHEMA_HASH)
        self.assertEqual(sum(len(points) for points in schema.CHECKPOINTS),
                         CHECKPOINTS)
        self.assertEqual(sum(ROUTE_COUNTS) + 1, SEGMENTS)
        self.assertEqual(ROUTE_COUNTS[10], 1)
        self.assertEqual(ROUTE_COUNTS[31], 1)
        for route in range(V5_ROUTES):
            if route not in TERMINAL_ONLY_CHANGED and route != B2_ROUTE:
                self.assertEqual(schema.CHECKPOINTS[route],
                                 schema.V5_CHECKPOINTS[route])
        self.assertEqual(
            schema.CHECKPOINTS[B2_ROUTE],
            (
                "scene=02:00;trigger=mario-status-enter;"
                "status=rollout;y>=3200",
                "scene=02:00;trigger=actor-damage-ordinal;"
                "actor=petey;value=1",
                "scene=02:00;trigger=actor-damage-ordinal;"
                "actor=petey;value=2",
                "scene=02:00;trigger=actor-damage-ordinal;"
                "actor=petey;value=3",
            ),
        )
        self.assertEqual(
            schema.V5_CHECKPOINTS[B2_ROUTE],
            (
                "scene=02:00|02:01;trigger=demo-start",
                "scene=02:00|02:01;trigger=actor-damage-ordinal;"
                "actor=petey;value=1",
                "scene=02:00|02:01;trigger=actor-damage-ordinal;"
                "actor=petey;value=2",
                "scene=02:00|02:01;trigger=actor-damage-ordinal;"
                "actor=petey;value=3",
            ),
        )
        self.assertTrue(all(not points for points in schema.CHECKPOINTS[V5_ROUTES:]))

    def test_source_route_table_preserves_one_retired_bianco_slot(self) -> None:
        routes = parse_route_table(SPLITS.read_text(encoding="utf-8"))
        expected = tuple(
            (ROUTE_FIRST[i], ROUTE_ENTRIES[i], ROUTE_COUNTS[i] - 1)
            for i in range(ROUTES)
        )
        self.assertEqual(routes, expected)
        self.assertEqual(routes[0][0], 0)
        for route, (previous, current) in enumerate(zip(routes, routes[1:]), 1):
            gap = 1 if route == B2_ROUTE + 1 else 0
            self.assertEqual(current[0], previous[0] + previous[2] + 1 + gap)
        self.assertEqual(routes[-1][0] + routes[-1][2] + 1, SEGMENTS)

    def test_route_enum_and_stats_cover_every_catalog_row(self) -> None:
        api = SPLIT_API.read_text(encoding="utf-8")
        enum = source_function(api, "enum RouteId", "struct Summary")
        values = [
            int(value)
            for value in re.findall(
                r"ROUTE_[A-Z0-9_]+\s*=\s*(\d+)\s*,", enum
            )
        ]
        self.assertEqual(values, list(range(ROUTES + 1)))
        source = SPLITS.read_text(encoding="utf-8")
        self.assertIn("bool supportsEntry(int entry) { return routeForEntry(entry) >= 0; }",
                      source)
        self.assertIn("out->segmentCount = segmentCount(desc);", source)
        self.assertIn("captureSegment(route, desc.checkpointCount, qf);", source)

        menu = MENU.read_text(encoding="utf-8")
        self.assertIn("SplitStats::supportsEntry(selectedEntry())", menu)
        self.assertIn("stats.segmentCount == 1", menu)
        self.assertIn('"Terminal"', menu)
        self.assertIn('"Time played"', menu)
        self.assertIn("formatPlayedTime(stats.playedQf", menu)

    def test_bianco_one_and_bianco_two_routing_are_not_aliased(self) -> None:
        self.assertEqual(ROUTE_ENTRIES[13], 1)
        self.assertEqual(ROUTE_ENTRIES[61], 0)
        source = SPLITS.read_text(encoding="utf-8")
        attempt = source_function(source, "int routeForAttempt(",
                                  "int routeForResult(")
        result = source_function(source, "int routeForResult(",
                                 "u8 segmentCount(")
        self.assertIn("entry == 0 && StageLoader::activeRouteMatches(0, 1)",
                      attempt)
        self.assertIn("return SplitStats::ROUTE_BIANCO_2;", attempt)
        self.assertIn("return routeForEntry(entry);", attempt)
        self.assertIn("sState->activeRoute == SplitStats::ROUTE_BIANCO_2",
                      result)
        self.assertIn("entry == 0", result)
        self.assertIn("return SplitStats::ROUTE_BIANCO_2;", result)
        self.assertIn("return routeForEntry(entry);", result)


class LayoutAndPersistenceContracts(unittest.TestCase):
    def test_v8_header_and_legacy_struct_sizes_are_frozen(self) -> None:
        header = HEADER.read_text(encoding="utf-8")
        expected = {
            "SUSAMUNE_SPLIT_STATS_VERSION": VERSION,
            "SUSAMUNE_SPLIT_STATS_ROUTE_COUNT": ROUTES,
            "SUSAMUNE_SPLIT_STATS_SEGMENT_COUNT": SEGMENTS,
            "SUSAMUNE_SPLIT_STATS_SCHEMA_HASH": SCHEMA_HASH,
            "SUSAMUNE_SPLIT_STATS_V7_ROUTE_COUNT": V7_ROUTES,
            "SUSAMUNE_SPLIT_STATS_V7_SEGMENT_COUNT": V7_SEGMENTS,
            "SUSAMUNE_SPLIT_STATS_V7_SCHEMA_HASH": V7_SCHEMA_HASH,
            "SUSAMUNE_SPLIT_STATS_V6_ROUTE_COUNT": V6_ROUTES,
            "SUSAMUNE_SPLIT_STATS_V6_SEGMENT_COUNT": V6_SEGMENTS,
            "SUSAMUNE_SPLIT_STATS_V6_SCHEMA_HASH": V6_SCHEMA_HASH,
            "SUSAMUNE_SPLIT_STATS_V5_ROUTE_COUNT": V5_ROUTES,
            "SUSAMUNE_SPLIT_STATS_V5_SEGMENT_COUNT": V5_SEGMENTS,
            "SUSAMUNE_SPLIT_STATS_V5_SCHEMA_HASH": V5_SCHEMA_HASH,
        }
        for name, value in expected.items():
            self.assertEqual(macro(header, name), value, name)
        self.assertRegex(
            header,
            r"routeStats\s*\[SUSAMUNE_SPLIT_STATS_REGION_COUNT\]\s*"
            r"\[SUSAMUNE_SPLIT_STATS_ROUTE_COUNT\];\s*"
            r"(?:/\*.*?\*/\s*|//[^\n]*\n\s*)*"
            r"unsigned int playedQf\s*\[SUSAMUNE_SPLIT_STATS_REGION_COUNT\]\s*"
            r"\[SUSAMUNE_SPLIT_STATS_ROUTE_COUNT\];\s*"
            r"unsigned int bestQf",
        )
        self.assertIn("sizeof(struct SusamuneSplitStatsPayload) == 0x744C",
                      header)
        self.assertIn("sizeof(struct SusamuneSplitStatsCfg) == 0x7500", header)
        self.assertIn("sizeof(struct SusamuneSplitStatsFile) == 0x74A0", header)
        self.assertIn("sizeof(struct SusamuneSplitStatsPayloadV7) == 0x6E34",
                      header)
        self.assertIn("sizeof(struct SusamuneSplitStatsFileV7) == 0x6FE0",
                      header)
        self.assertIn("sizeof(struct SusamuneSplitStatsPayloadV6) == 0x6DF8",
                      header)
        self.assertIn("sizeof(struct SusamuneSplitStatsFileV6) == 0x6FE0",
                      header)
        self.assertIn("sizeof(struct SusamuneSplitStatsPayloadV5) == 0x46E0",
                      header)
        self.assertIn("sizeof(struct SusamuneSplitStatsFileV5) == 0x47E0",
                      header)

    def test_payload_size_arithmetic_and_symbolic_mailbox_bounds(self) -> None:
        route_stats = REGIONS * ROUTES * 12
        played = REGIONS * ROUTES * 4
        best = REGIONS * SEGMENTS * 4
        identities = REGIONS * PROFILES * ROUTES * 4
        pb_segments = REGIONS * PROFILES * SEGMENTS * 4
        self.assertEqual(route_stats + played + best + identities + pb_segments,
                         PAYLOAD_SIZE)
        self.assertEqual(CFG_OFFSET + CFG_SIZE, 0xF780)
        self.assertLessEqual(CFG_OFFSET + CFG_SIZE, 0x10000)

        header = HEADER.read_text(encoding="utf-8")
        self.assertEqual(macro(header, "SUSAMUNE_SPLIT_STATS_CFG_OFFSET"),
                         CFG_OFFSET)
        self.assertIn(
            "SUSAMUNE_SPLIT_STATS_CFG_OFFSET + sizeof(struct SusamuneSplitStatsCfg) ==",
            header,
        )
        self.assertIn("SUSAMUNE_MEM2_PB_LIVE_PPC_BASE - SUSAMUNE_MEM2_CFG_PPC_BASE",
                      header)
        self.assertIn("SUSAMUNE_DOLPHIN_PB_LIVE_PPC_BASE - SUSAMUNE_DOLPHIN_RUNTIME_PPC_BASE",
                      header)

    def test_v8_uses_new_files_and_v7_is_read_only_migration_input(self) -> None:
        kernel = KERNEL.read_text(encoding="utf-8")
        self.assertIn("susamune_il_stats_v8_a.bin", kernel)
        self.assertIn("susamune_il_stats_v8_b.bin", kernel)
        self.assertIn("susamune_il_stats_v7_a.bin", kernel)
        self.assertIn("susamune_il_stats_v7_b.bin", kernel)
        self.assertIn("susamune_il_stats_v6_a.bin", kernel)
        self.assertIn("susamune_il_stats_v6_b.bin", kernel)
        self.assertIn("susamune_il_stats_v5_a.bin", kernel)
        self.assertIn("susamune_il_stats_v5_b.bin", kernel)
        init = function_block(kernel, "static bool InitSplitStatsFiles(")
        self.assertIn("ReadSplitStatsV7File", init)
        self.assertIn("MigrateSplitStatsV7", init)
        self.assertIn("ReadSplitStatsV6File", init)
        self.assertIn("MigrateSplitStatsV6", init)
        self.assertIn("ReadSplitStatsV5File", init)
        self.assertIn("MigrateSplitStatsV5", init)
        writer = function_block(kernel, "static int WriteSplitStatsFile(")
        self.assertIn("SplitStatsPaths[target]", writer)
        self.assertNotIn("v7Paths[target]", writer)
        self.assertNotIn("v6Paths[target]", writer)
        self.assertNotIn("v5Paths[target]", writer)
        self.assertNotIn("susamune_il_stats_v6", writer)
        self.assertNotIn("susamune_il_stats_v5", writer)

    def test_future_or_unknown_current_files_disable_writes_and_fallback(self) -> None:
        kernel = KERNEL.read_text(encoding="utf-8")
        reader = function_block(
            kernel, "static enum PbReadResult ReadSplitStatsFile("
        )
        self.assertIn("file->magic == SUSAMUNE_SPLIT_STATS_FILE_MAGIC", reader)
        self.assertIn("file->version != SUSAMUNE_SPLIT_STATS_VERSION", reader)
        self.assertIn(
            "file->schemaHash != SUSAMUNE_SPLIT_STATS_SCHEMA_HASH", reader
        )
        supported = function_block(kernel, "static bool SplitStatsV7SchemaSupported(")
        self.assertIn("SUSAMUNE_SPLIT_STATS_V7_SCHEMA_HASH", supported)
        self.assertIn("SUSAMUNE_SPLIT_STATS_PREVIOUS_SCHEMA_HASH", supported)
        self.assertGreaterEqual(reader.count("return PB_READ_UNSAFE;"), 3)

        init = function_block(kernel, "static bool InitSplitStatsFiles(")
        unsafe = init.index("if (readResult == PB_READ_UNSAFE)")
        unsafe_tail = init[unsafe:unsafe + 180]
        self.assertIn("safe = false;", unsafe_tail)
        v7_fallback = init.index("ReadSplitStatsV7File")
        self.assertIn("if (safe && SplitStatsActiveFile < 0", init[:v7_fallback])
        writable = init.rindex("SUSAMUNE_SPLIT_STATS_FLAG_WRITABLE")
        self.assertIn("if (safe)", init[writable - 120:writable])

    def test_played_time_is_hashed_but_not_pb_range_validated(self) -> None:
        kernel = KERNEL.read_text(encoding="utf-8")
        checksum = function_block(kernel, "static u32 SplitStatsChecksum(")
        self.assertIn("payload->playedQf[region][route]", checksum)
        valid = function_block(kernel, "static bool SplitStatsPayloadValid(")
        self.assertIn("payload->bestQf[region][segment]", valid)
        self.assertNotIn("playedQf", valid)
        source = SPLITS.read_text(encoding="utf-8")
        valid_mod = function_block(source, "bool payloadValid(")
        self.assertIn("&payload.bestQf[0][0]", valid_mod)
        self.assertNotIn("&payload.playedQf[0][0]", valid_mod)


class MigrationContracts(unittest.TestCase):
    def populated_v5(self) -> dict[str, list]:
        old = blank_payload(V5_ROUTES, V5_SEGMENTS, played=False)
        for region in range(REGIONS):
            for route in range(V5_ROUTES):
                old["stats"][region][route] = [
                    1000 + region * 100 + route,
                    500 + region * 10 + route,
                    70 + route,
                ]
                first = V5_ROUTE_FIRST[route]
                for local in range(V5_ROUTE_COUNTS[route]):
                    old["best"][region][first + local] = (
                        10000 + region * 1000 + route * 10 + local
                    )
                for profile in range(PROFILES):
                    old["identity"][region][profile][route] = (
                        20000 + region * 2000 + profile * 200 + route
                    )
                    for local in range(V5_ROUTE_COUNTS[route]):
                        old["pb"][region][profile][first + local] = (
                            30000 + region * 3000 + profile * 300 +
                            route * 10 + local
                        )
        return old

    def populated_v6(self) -> dict[str, list]:
        old = blank_payload(V6_ROUTES, V6_SEGMENTS, played=True)
        for region in range(REGIONS):
            for route in range(V6_ROUTES):
                old["stats"][region][route] = [
                    4000 + region * 100 + route,
                    2000 + region * 10 + route,
                    90 + route,
                ]
                old["played"][region][route] = (
                    5000 + region * 1000 + route
                )
                first = V6_ROUTE_FIRST[route]
                for local in range(V6_ROUTE_COUNTS[route]):
                    old["best"][region][first + local] = (
                        10000 + region * 1000 + route * 10 + local
                    )
                for profile in range(PROFILES):
                    old["identity"][region][profile][route] = (
                        20000 + region * 2000 + profile * 200 + route
                    )
                    for local in range(V6_ROUTE_COUNTS[route]):
                        old["pb"][region][profile][first + local] = (
                            30000 + region * 3000 + profile * 300 +
                            route * 10 + local
                        )
        return old

    def test_v5_migration_preserves_every_compatible_route_locally(self) -> None:
        old = self.populated_v5()
        new = migrate_v5(old)
        for region in range(REGIONS):
            self.assertEqual(new["played"][region], [0] * ROUTES)
            for route in range(V5_ROUTES):
                self.assertEqual(new["stats"][region][route][:2],
                                 old["stats"][region][route][:2])
                self.assertEqual(
                    new["stats"][region][route][2],
                    0 if route in TERMINAL_ONLY_CHANGED or route == B2_ROUTE
                    else old["stats"][region][route][2],
                )
                if route == B2_ROUTE:
                    new_first = ROUTE_FIRST[route]
                    old_first = V5_ROUTE_FIRST[route]
                    self.assertEqual(
                        new["best"][region][new_first:new_first + 1],
                        [UNSET],
                    )
                    self.assertEqual(
                        new["best"][region][new_first + 1:new_first + 5],
                        old["best"][region][old_first + 1:old_first + 5],
                    )
                elif route in TERMINAL_ONLY_CHANGED:
                    self.assertEqual(new["best"][region][ROUTE_FIRST[route]],
                                     UNSET)
                else:
                    count = ROUTE_COUNTS[route]
                    self.assertEqual(
                        new["best"][region]
                           [ROUTE_FIRST[route]:ROUTE_FIRST[route] + count],
                        old["best"][region]
                           [V5_ROUTE_FIRST[route]:V5_ROUTE_FIRST[route] + count],
                    )
                for profile in range(PROFILES):
                    identity = old["identity"][region][profile][route]
                    if route == B2_ROUTE:
                        self.assertEqual(
                            new["identity"][region][profile][route], UNSET
                        )
                        new_first = ROUTE_FIRST[route]
                        self.assertEqual(
                            new["pb"][region][profile]
                               [new_first:new_first + ROUTE_COUNTS[route]],
                            [UNSET] * ROUTE_COUNTS[route],
                        )
                    elif route in TERMINAL_ONLY_CHANGED:
                        self.assertEqual(
                            new["identity"][region][profile][route], identity
                        )
                        self.assertEqual(
                            new["pb"][region][profile][ROUTE_FIRST[route]],
                            identity,
                        )
                    else:
                        self.assertEqual(
                            new["identity"][region][profile][route], identity
                        )
                        count = ROUTE_COUNTS[route]
                        self.assertEqual(
                            new["pb"][region][profile]
                               [ROUTE_FIRST[route]:ROUTE_FIRST[route] + count],
                            old["pb"][region][profile]
                               [V5_ROUTE_FIRST[route]:V5_ROUTE_FIRST[route] + count],
                        )

    def test_v6_migration_resets_only_incompatible_b2_baseline(self) -> None:
        old = self.populated_v6()
        new = migrate_v6(old)
        for region in range(REGIONS):
            for route in range(V6_ROUTES):
                self.assertEqual(new["stats"][region][route][:2],
                                 old["stats"][region][route][:2])
                self.assertEqual(new["played"][region][route],
                                 old["played"][region][route])
                expected_golds = (
                    0 if route == B2_ROUTE
                    else old["stats"][region][route][2]
                )
                self.assertEqual(new["stats"][region][route][2],
                                 expected_golds)
                new_first = ROUTE_FIRST[route]
                old_first = V6_ROUTE_FIRST[route]
                if route == B2_ROUTE:
                    self.assertEqual(
                        new["best"][region][new_first:new_first + 1],
                        [UNSET],
                    )
                    self.assertEqual(
                        new["best"][region][new_first + 1:new_first + 5],
                        old["best"][region][old_first + 1:old_first + 5],
                    )
                else:
                    count = V6_ROUTE_COUNTS[route]
                    self.assertEqual(
                        new["best"][region][new_first:new_first + count],
                        old["best"][region][old_first:old_first + count],
                    )
                for profile in range(PROFILES):
                    if route == B2_ROUTE:
                        self.assertEqual(
                            new["identity"][region][profile][route], UNSET
                        )
                        self.assertEqual(
                            new["pb"][region][profile]
                               [new_first:new_first + ROUTE_COUNTS[route]],
                            [UNSET] * ROUTE_COUNTS[route],
                        )
                    else:
                        self.assertEqual(
                            new["identity"][region][profile][route],
                            old["identity"][region][profile][route],
                        )
                        count = V6_ROUTE_COUNTS[route]
                        self.assertEqual(
                            new["pb"][region][profile]
                               [new_first:new_first + count],
                            old["pb"][region][profile]
                               [old_first:old_first + count],
                        )
            for route in range(V6_ROUTES, ROUTES):
                self.assertEqual(new["stats"][region][route], [0, 0, 0])
                self.assertEqual(new["played"][region][route], 0)
                self.assertEqual(new["best"][region][ROUTE_FIRST[route]], UNSET)
                for profile in range(PROFILES):
                    self.assertEqual(new["identity"][region][profile][route],
                                     UNSET)
                    self.assertEqual(
                        new["pb"][region][profile][ROUTE_FIRST[route]], UNSET
                    )

    def test_v5_terminal_only_unset_identity_remains_unset(self) -> None:
        old = self.populated_v5()
        old["identity"][2][3][10] = UNSET
        old["identity"][1][1][31] = UNSET
        new = migrate_v5(old)
        self.assertEqual(new["pb"][2][3][ROUTE_FIRST[10]], UNSET)
        self.assertEqual(new["pb"][1][1][ROUTE_FIRST[31]], UNSET)

    def test_new_routes_are_clean_after_v5_migration(self) -> None:
        new = migrate_v5(self.populated_v5())
        for region in range(REGIONS):
            for route in range(V5_ROUTES, ROUTES):
                self.assertEqual(new["stats"][region][route], [0, 0, 0])
                self.assertEqual(new["played"][region][route], 0)
                self.assertEqual(new["best"][region][ROUTE_FIRST[route]], UNSET)
                for profile in range(PROFILES):
                    self.assertEqual(new["identity"][region][profile][route],
                                     UNSET)
                    self.assertEqual(
                        new["pb"][region][profile][ROUTE_FIRST[route]], UNSET
                    )

    def test_kernel_route_arrays_and_special_migration_match_model(self) -> None:
        kernel = KERNEL.read_text(encoding="utf-8")
        self.assertEqual(parse_numeric_array(kernel, "SplitRouteFirst"),
                         ROUTE_FIRST)
        self.assertEqual(parse_numeric_array(kernel, "SplitRouteCount"),
                         ROUTE_COUNTS)
        self.assertEqual(parse_numeric_array(kernel, "SplitV5RouteFirst"),
                         V5_ROUTE_FIRST)
        self.assertEqual(parse_numeric_array(kernel, "SplitV5RouteCount"),
                         V5_ROUTE_COUNTS)
        self.assertEqual(parse_numeric_array(kernel, "SplitV6RouteFirst"),
                         V6_ROUTE_FIRST)
        self.assertEqual(parse_numeric_array(kernel, "SplitV6RouteCount"),
                         V6_ROUTE_COUNTS)
        terminal = function_block(
            kernel, "static bool SplitStatsV5RouteBecameTerminal("
        )
        self.assertIn("route == 10 || route == 31", terminal)
        migration = function_block(kernel, "static void MigrateSplitStatsV5(")
        self.assertIn("SplitStatsV5RouteBecameTerminal(route)", migration)
        self.assertIn("dst->routeStats[region][route].golds", migration)
        self.assertIn("dst->bestQf[region]", migration)
        self.assertIn("src->pbIdentityQf[region][profile][route]", migration)
        self.assertIn("dst->pbQf[region][profile]", migration)
        migration_v6 = function_block(
            kernel, "static void MigrateSplitStatsV6("
        )
        self.assertIn("const bool b2 = route == 13", migration_v6)
        self.assertIn("dst->playedQf[region][route]", migration_v6)
        self.assertIn("dst->routeStats[region][route].golds = 0", migration_v6)
        self.assertIn("if (b2)\n\t\t\t\t\tcontinue;", migration_v6)
        init = function_block(kernel, "static bool InitSplitStatsFiles(")
        migrate_v6_call = init.index("MigrateSplitStatsV6")
        self.assertIn("ResetSplitStatsPayload", init[:migrate_v6_call])
        migrate_call = init.index("MigrateSplitStatsV5")
        self.assertIn("ResetSplitStatsPayload", init[:migrate_call])


class PlayedTimeContracts(unittest.TestCase):
    def test_sampling_is_delta_based_and_summary_does_not_double_count(self) -> None:
        tracker = PlayedTracker(committed=500)
        tracker.start(12)
        tracker.sample(52)
        self.assertEqual(tracker.summary(80), 568)
        self.assertEqual(tracker.summary(80), 568)
        tracker.sample(80)
        self.assertEqual(tracker.summary(80), 568)
        tracker.commit(100)
        self.assertEqual(tracker.committed, 588)

    def test_assist_commits_elapsed_once_then_stops(self) -> None:
        tracker = PlayedTracker(committed=100)
        tracker.start(20)
        tracker.invalidate(75)
        self.assertEqual(tracker.committed, 155)
        tracker.sample(200)
        tracker.commit(300)
        self.assertEqual(tracker.committed, 155)
        self.assertEqual(tracker.summary(400), 155)

    def test_time_saturates_and_ignores_non_monotonic_samples(self) -> None:
        self.assertEqual(saturating_add(UNSET - 2, 9), UNSET)
        tracker = PlayedTracker(committed=UNSET - 5)
        tracker.start(100)
        tracker.sample(90)
        self.assertEqual(tracker.summary(90), UNSET - 5)
        tracker.commit(110)
        self.assertEqual(tracker.committed, UNSET)

    def test_production_commits_boundaries_and_exposes_live_summary(self) -> None:
        source = SPLITS.read_text(encoding="utf-8")
        self.assertIn("gQFTTimer.currentQf", source)
        self.assertRegex(source, r"0xffffffffu\s*-|0xFFFFFFFFu\s*-")

        update = function_block(source, "void update()")
        self.assertIn("sampleAttemptTime", update)
        started = function_block(source, "void onILAttemptStarted(")
        self.assertIn("beginAttemptTime();", started)
        self.assertGreater(started.index("beginAttemptTime();"),
                           started.index("sState->activeRoute = (u8)route;"))
        invalidate = function_block(source, "void invalidateAttempt()")
        self.assertIn("commitAttemptTime", invalidate)
        result = function_block(source, "void onILResult(")
        self.assertIn("commitAttemptTime(qf);", result)
        ended = function_block(source, "void onILAttemptEnded()")
        self.assertIn("endAttempt", ended)
        savestate = function_block(source, "void onSavestateLoaded()")
        self.assertIn("commitAttemptTime", savestate)

        summary = function_block(source, "bool summary(")
        self.assertIn("out->playedQf", summary)
        self.assertIn("pendingAttemptTime(route)", summary)
        pending = function_block(source, "u32 pendingAttemptTime(")
        self.assertIn("sState->activeRoute != route", pending)
        self.assertIn("gQFTTimer.currentQf", pending)

        sample = function_block(source, "void sampleAttemptTime(")
        self.assertNotIn("markDirty", sample)
        commit = function_block(source, "void commitAttemptTime(")
        self.assertIn("markDirty", commit)


if __name__ == "__main__":
    unittest.main()
