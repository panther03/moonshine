"""Contracts for the same-version Bianco 2 split-schema migration."""

from __future__ import annotations

from copy import deepcopy
import importlib.util
from itertools import accumulate
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "include" / "susamune" / "susamune_cfg.h"
KERNEL = ROOT / "launcher" / "kernel" / "SusamuneCfg.c"
SCHEMA_PATH = ROOT / "scripts" / "split_checkpoint_schema.py"

spec = importlib.util.spec_from_file_location("split_schema_pr3", SCHEMA_PATH)
assert spec and spec.loader
schema = importlib.util.module_from_spec(spec)
spec.loader.exec_module(schema)

CURRENT_SCHEMA = 0xB933B5AB
PREVIOUS_SCHEMA = 0x4499A650
UNSET = 0xFFFFFFFF
REGIONS = 3
PROFILES = 4
B2_ROUTE = 13
ROUTE_COUNTS = tuple(len(checkpoints) + 1 for checkpoints in schema.CHECKPOINTS)
ROUTE_FIRST = (0,) + tuple(accumulate(ROUTE_COUNTS[:-1]))
ROUTES = len(ROUTE_COUNTS)
SEGMENTS = sum(ROUTE_COUNTS)


def function_block(text: str, signature: str) -> str:
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


def blank_payload() -> dict[str, list]:
    return {
        "stats": [[[0, 0, 0] for _ in range(ROUTES)] for _ in range(REGIONS)],
        "played": [[0] * ROUTES for _ in range(REGIONS)],
        "best": [[UNSET] * SEGMENTS for _ in range(REGIONS)],
        "identity": [
            [[UNSET] * ROUTES for _ in range(PROFILES)] for _ in range(REGIONS)
        ],
        "pb": [
            [[UNSET] * SEGMENTS for _ in range(PROFILES)]
            for _ in range(REGIONS)
        ],
    }


def populated_payload() -> dict[str, list]:
    payload = blank_payload()
    for region in range(REGIONS):
        for route in range(ROUTES):
            payload["stats"][region][route] = [
                1000 + region * ROUTES + route,
                500 + region * ROUTES + route,
                100 + region * ROUTES + route,
            ]
            payload["played"][region][route] = 2000 + region * ROUTES + route
        for segment in range(SEGMENTS):
            payload["best"][region][segment] = 3000 + region * SEGMENTS + segment
        for profile in range(PROFILES):
            for route in range(ROUTES):
                payload["identity"][region][profile][route] = (
                    4000 + (region * PROFILES + profile) * ROUTES + route
                )
            for segment in range(SEGMENTS):
                payload["pb"][region][profile][segment] = (
                    5000 + (region * PROFILES + profile) * SEGMENTS + segment
                )
    return payload


def migrate_previous(payload: dict[str, list]) -> dict[str, list]:
    migrated = deepcopy(payload)
    first = ROUTE_FIRST[B2_ROUTE]
    end = first + ROUTE_COUNTS[B2_ROUTE]
    for region in range(REGIONS):
        migrated["stats"][region][B2_ROUTE][2] = 0
        migrated["best"][region][first:end] = [UNSET] * (end - first)
        for profile in range(PROFILES):
            migrated["identity"][region][profile][B2_ROUTE] = UNSET
            migrated["pb"][region][profile][first:end] = [UNSET] * (end - first)
    return migrated


class PreviousSchemaMigrationContracts(unittest.TestCase):
    def test_hashes_are_explicit_and_current_schema_is_unchanged(self) -> None:
        header = HEADER.read_text(encoding="utf-8")
        self.assertEqual(schema.EXPECTED_SCHEMA_HASH, CURRENT_SCHEMA)
        self.assertRegex(
            header,
            rf"SUSAMUNE_SPLIT_STATS_SCHEMA_HASH\s+0x{CURRENT_SCHEMA:08X}u",
        )
        self.assertRegex(
            header,
            rf"SUSAMUNE_SPLIT_STATS_PREVIOUS_SCHEMA_HASH\s+0x{PREVIOUS_SCHEMA:08X}u",
        )

    def test_reader_accepts_only_current_and_immediately_previous_v7(self) -> None:
        kernel = KERNEL.read_text(encoding="utf-8")
        supported = function_block(kernel, "static bool SplitStatsSchemaSupported(")
        reader = function_block(kernel, "static enum PbReadResult ReadSplitStatsFile(")
        self.assertIn("schemaHash == SUSAMUNE_SPLIT_STATS_SCHEMA_HASH", supported)
        self.assertIn(
            "schemaHash == SUSAMUNE_SPLIT_STATS_PREVIOUS_SCHEMA_HASH", supported
        )
        self.assertEqual(supported.count("schemaHash =="), 2)
        self.assertGreaterEqual(reader.count("SplitStatsSchemaSupported"), 3)
        self.assertIn("file->checksum != SplitStatsChecksum(file)", reader)

    def test_only_incompatible_b2_timing_history_is_invalidated(self) -> None:
        old = populated_payload()
        new = migrate_previous(old)
        first = ROUTE_FIRST[B2_ROUTE]
        end = first + ROUTE_COUNTS[B2_ROUTE]

        for region in range(REGIONS):
            for route in range(ROUTES):
                if route == B2_ROUTE:
                    self.assertEqual(
                        new["stats"][region][route][:2],
                        old["stats"][region][route][:2],
                    )
                    self.assertEqual(new["stats"][region][route][2], 0)
                else:
                    self.assertEqual(
                        new["stats"][region][route], old["stats"][region][route]
                    )
            self.assertEqual(new["played"][region], old["played"][region])
            self.assertEqual(new["best"][region][:first], old["best"][region][:first])
            self.assertEqual(new["best"][region][first:end], [UNSET] * (end - first))
            self.assertEqual(new["best"][region][end:], old["best"][region][end:])

            for profile in range(PROFILES):
                for route in range(ROUTES):
                    expected = (
                        UNSET
                        if route == B2_ROUTE
                        else old["identity"][region][profile][route]
                    )
                    self.assertEqual(new["identity"][region][profile][route], expected)
                self.assertEqual(
                    new["pb"][region][profile][:first],
                    old["pb"][region][profile][:first],
                )
                self.assertEqual(
                    new["pb"][region][profile][first:end],
                    [UNSET] * (end - first),
                )
                self.assertEqual(
                    new["pb"][region][profile][end:],
                    old["pb"][region][profile][end:],
                )

    def test_kernel_migration_matches_policy_and_requests_rewrite(self) -> None:
        kernel = KERNEL.read_text(encoding="utf-8")
        migration = function_block(
            kernel, "static void MigrateSplitStatsPreviousSchema("
        )
        init = function_block(kernel, "static bool InitSplitStatsFiles(")
        self.assertIn("const u32 route = 13", migration)
        self.assertIn("routeStats[region][route].golds = 0", migration)
        self.assertIn("&payload->bestQf[region][first]", migration)
        self.assertIn("pbIdentityQf[region][profile][route]", migration)
        self.assertIn("&payload->pbQf[region][profile][first]", migration)
        self.assertNotIn("playedQf", migration)
        self.assertIn("file->schemaHash != selectedSchemaHash", init)
        self.assertIn("selectedSchemaHash = file->schemaHash", init)
        self.assertIn(
            "selectedSchemaHash == SUSAMUNE_SPLIT_STATS_PREVIOUS_SCHEMA_HASH",
            init,
        )
        self.assertIn("MigrateSplitStatsPreviousSchema(&stats->payload)", init)
        self.assertIn("migrated = true", init)
        self.assertLess(
            init.index("MigrateSplitStatsPreviousSchema"),
            init.index("ReadSplitStatsV6File"),
        )


if __name__ == "__main__":
    unittest.main()
