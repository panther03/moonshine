#!/usr/bin/env python3
"""Host policy tests for the console fixed-slot ghost service."""

from __future__ import annotations

from datetime import date
from pathlib import Path
import re
import struct
import sys
import unittest
from unittest import mock
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_ghost_format import build_ghost
import validate_ghost as ghost_format
import validate_ghost_storage as storage


def source_function(path: Path, name: str) -> str:
    source = path.read_text(encoding="utf-8")
    match = re.search(
        rf"(?:static\s+)?(?:void|bool|int|enum\s+ValidateResult)\s+"
        rf"{name}\s*\(",
        source,
    )
    if match is None:
        raise AssertionError(f"could not find {name} in {path}")
    begin = source.index("{", match.end())
    depth = 0
    for end in range(begin, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():end + 1]
    raise AssertionError(f"unterminated {name} in {path}")


def envelope(
    payload: bytes = b"", *, generation: int = 1, profile: int = 0,
    slot: int = 0, flags: int = 0, version: int = storage.ENVELOPE_VERSION,
) -> bytes:
    duration = struct.unpack_from(">I", payload, 64)[0] if payload else 0
    payload_crc = zlib.crc32(payload) & 0xFFFFFFFF if payload else 0
    values = [
        storage.ENVELOPE_MAGIC, version, storage.ENVELOPE_SIZE,
        generation, flags, ghost_format.REGION_GAME_IDS[0], profile, slot,
        len(payload), duration, payload_crc, 0, *([0] * 6),
    ]
    header = bytearray(storage._ENVELOPE.pack(*values))
    struct.pack_into(">I", header, 36, zlib.crc32(header) & 0xFFFFFFFF)
    return bytes(header) + payload


def rechecksum_ghost(raw: bytes) -> bytes:
    work = bytearray(raw)
    struct.pack_into(">I", work, 20, zlib.crc32(
        work[ghost_format.GHOST_HEADER_SIZE:]
    ) & 0xFFFFFFFF)
    struct.pack_into(">II", work, 12, 0, 0)
    struct.pack_into(">I", work, 16, zlib.crc32(
        work[:ghost_format.GHOST_HEADER_SIZE]
    ) & 0xFFFFFFFF)
    struct.pack_into(">I", work, 12, 0)
    struct.pack_into(">I", work, 12, zlib.crc32(work) & 0xFFFFFFFF)
    return bytes(work)


class StorageEnvelopeTests(unittest.TestCase):
    def test_old_and_future_canonical_versions_are_not_parsed(self) -> None:
        for version in (ghost_format.GHOST_VERSION_V1,
                        ghost_format.GHOST_VERSION_V2, 5):
            with self.subTest(version=version), mock.patch.object(
                ghost_format,
                "validate_ghost",
                side_effect=AssertionError("unsupported body was parsed"),
            ):
                prefix = ghost_format.GHOST_MAGIC + struct.pack(">H", version)
                with self.assertRaises(storage.UnsupportedStorage):
                    storage._validate_canonical_ghost(prefix + bytes(512))

    def test_valid_ghost_and_tombstone(self) -> None:
        ghost = envelope(build_ghost())
        parsed = storage.validate_slot_file(
            ghost, game_id=ghost_format.REGION_GAME_IDS[0], profile=0, slot=0
        )
        self.assertIsNotNone(parsed["ghost"])
        tombstone = envelope(flags=storage.TOMBSTONE, generation=2)
        parsed = storage.validate_slot_file(
            tombstone, game_id=ghost_format.REGION_GAME_IDS[0], profile=0, slot=0
        )
        self.assertIsNone(parsed["ghost"])

    def test_storage_and_share_accept_v3_and_v4(self) -> None:
        for version in (ghost_format.GHOST_VERSION_V3,
                        ghost_format.GHOST_VERSION_V4):
            payload = build_ghost(version=version)
            parsed = storage.validate_slot_file(
                envelope(payload),
                game_id=ghost_format.REGION_GAME_IDS[0], profile=0, slot=0,
            )
            self.assertEqual(parsed["ghost"]["version"], version)
            shared = storage.validate_share_file(
                payload, game_id=ghost_format.REGION_GAME_IDS[0], profile=0,
            )
            self.assertEqual(shared["version"], version)

        payload = build_ghost()
        for version in (ghost_format.GHOST_VERSION_V1,
                        ghost_format.GHOST_VERSION_V2):
            old = bytearray(payload)
            struct.pack_into(">H", old, 4, version)
            with self.subTest(version=version):
                with self.assertRaises(storage.UnsupportedStorage):
                    storage.validate_slot_file(
                        envelope(bytes(old)),
                        game_id=ghost_format.REGION_GAME_IDS[0],
                        profile=0,
                        slot=0,
                    )
                with self.assertRaises(storage.UnsupportedStorage):
                    storage.validate_share_file(
                        bytes(old),
                        game_id=ghost_format.REGION_GAME_IDS[0],
                        profile=0,
                    )

    def test_storage_rejects_malformed_v3_pose_samples(self) -> None:
        base = build_ghost(version=ghost_format.GHOST_VERSION_V3)
        sample = ghost_format.V3_SAMPLE_DATA_OFFSET
        bad_position = bytearray(base)
        bad_position[sample + 4:sample + 7] = (
            ghost_format.MAX_POSITION_FIXED + 1
        ).to_bytes(3, "big")

        bad_delta = bytearray(base)
        struct.pack_into(">H", bad_delta, sample + 2, 1)

        bad_animation_id = bytearray(base)
        bad_animation_id[sample + 13:sample + 16] = (
            (ghost_format.ANIMATION_ID_MAX + 1) << 15
        ).to_bytes(3, "big")

        bad_animation_reserved = bytearray(base)
        animation = int.from_bytes(
            bad_animation_reserved[sample + 13:sample + 16], "big"
        )
        bad_animation_reserved[sample + 13:sample + 16] = (
            animation | 1
        ).to_bytes(3, "big")

        for label, malformed in (
            ("position", bad_position),
            ("first delta", bad_delta),
            ("animation id", bad_animation_id),
            ("animation reserved", bad_animation_reserved),
        ):
            with self.subTest(label=label), self.assertRaises(
                storage.StorageError
            ):
                storage.validate_slot_file(
                    envelope(rechecksum_ghost(bytes(malformed))),
                    game_id=ghost_format.REGION_GAME_IDS[0],
                    profile=0,
                    slot=0,
                )

    def test_storage_rejects_version_feature_codec_mismatches(self) -> None:
        v4_missing_feature = bytearray(build_ghost())
        struct.pack_into(">I", v4_missing_feature, 24, 0)
        v4_raw_codec = bytearray(build_ghost())
        v4_raw_codec[40] = ghost_format.CODEC_RAW
        v3_attachment_codec = bytearray(build_ghost(
            version=ghost_format.GHOST_VERSION_V3
        ))
        v3_attachment_codec[40] = ghost_format.CODEC_POSE_ATTACHMENTS
        for label, malformed in (
            ("V4 feature", v4_missing_feature),
            ("V4 codec", v4_raw_codec),
            ("V3 codec", v3_attachment_codec),
        ):
            with self.subTest(label=label), self.assertRaises(
                storage.StorageError
            ):
                storage.validate_slot_file(
                    envelope(rechecksum_ghost(bytes(malformed))),
                    game_id=ghost_format.REGION_GAME_IDS[0],
                    profile=0,
                    slot=0,
                )

    def test_v3_pose_extreme_phase_and_yaw_are_valid(self) -> None:
        raw = bytearray(build_ghost(version=ghost_format.GHOST_VERSION_V3))
        sample = ghost_format.V3_SAMPLE_DATA_OFFSET
        struct.pack_into(">h", raw, sample, -0x8000)
        animation = (
            ghost_format.ANIMATION_ID_MAX << 15
        ) | (ghost_format.ANIMATION_PHASE_MAX << 3)
        raw[sample + 13:sample + 16] = animation.to_bytes(3, "big")
        parsed = storage.validate_share_file(
            rechecksum_ghost(bytes(raw)),
            game_id=ghost_format.REGION_GAME_IDS[0],
            profile=0,
        )
        self.assertEqual(parsed["version"], ghost_format.GHOST_VERSION_V3)

    def test_corrupt_new_bank_falls_back(self) -> None:
        old = envelope(build_ghost(ghost_id=1), generation=4)
        corrupt = bytearray(envelope(build_ghost(ghost_id=2), generation=5))
        corrupt[-1] ^= 1
        selected = storage.choose_slot(
            (old, bytes(corrupt)), game_id=ghost_format.REGION_GAME_IDS[0],
            profile=0, slot=0,
        )
        self.assertEqual(selected[0], 0)
        self.assertEqual(selected[1]["generation"], 4)

    def test_future_bank_fails_closed(self) -> None:
        old = envelope(build_ghost(), generation=4)
        future = envelope(version=2, generation=5)
        with self.assertRaises(storage.UnsupportedStorage):
            storage.choose_slot(
                (old, future), game_id=ghost_format.REGION_GAME_IDS[0],
                profile=0, slot=0,
            )

    def test_equal_generation_conflict_is_unsafe(self) -> None:
        a = envelope(build_ghost(ghost_id=1), generation=7)
        b = envelope(build_ghost(ghost_id=2), generation=7)
        with self.assertRaises(storage.UnsupportedStorage):
            storage.choose_slot(
                (a, b), game_id=ghost_format.REGION_GAME_IDS[0],
                profile=0, slot=0,
            )

    def test_save_never_overwrites(self) -> None:
        self.assertTrue(storage.personal_slot_is_live(44))
        self.assertFalse(storage.personal_slot_is_live(45))
        self.assertFalse(storage.personal_slot_is_live(47))
        self.assertEqual(
            storage.save_status(occupied=True, request_flags=0),
            "slot_occupied",
        )
        self.assertEqual(
            storage.save_status(occupied=True, request_flags=1),
            "invalid_request",
        )
        self.assertEqual(
            storage.save_status(occupied=False, request_flags=0), "ok"
        )

    def test_kernel_ambiguous_errors_force_catalog_rescan(self) -> None:
        root = Path(__file__).resolve().parents[1]
        kernel = root / "launcher/kernel/SusamuneGhost.c"
        for function in ("FinishIoError", "SaveFinishPass",
                         "DeleteFinishPass"):
            self.assertIn(
                "InvalidateRequestCatalog();",
                source_function(kernel, function),
            )
        self.assertIn(
            "if (Request.flags != 0)", source_function(kernel, "StartRequest")
        )
        self.assertNotIn(
            "ALLOW_OVERWRITE",
            kernel.read_text(encoding="utf-8") +
            (root / "include/susamune/ghost_storage.h").read_text(
                encoding="utf-8"
            ),
        )

    def test_kernel_and_ppc_catalog_accept_v3_and_v4_pose_files(self) -> None:
        root = Path(__file__).resolve().parents[1]
        kernel_path = root / "launcher/kernel/SusamuneGhost.c"
        ppc_path = root / "src/ghost_storage.cpp"
        header = source_function(kernel_path, "ValidateCanonicalHeader")
        self.assertIn(
            "version != SUSAMUNE_GHOST_FILE_VERSION_V3", header
        )
        self.assertIn(
            "version != SUSAMUNE_GHOST_FILE_VERSION_V4", header
        )
        self.assertNotIn("SUSAMUNE_GHOST_FILE_VERSION_V1", header)
        self.assertNotIn("SUSAMUNE_GHOST_FILE_VERSION_V2", header)
        self.assertLess(
            header.index("version != SUSAMUNE_GHOST_FILE_VERSION_V3"),
            header.index("headerCrc ="),
        )
        self.assertIn("SUSAMUNE_GHOST_RECORDING_POSE_QF", header)
        self.assertIn("SUSAMUNE_GHOST_POSE_SAMPLE_SIZE", header)

        begin = source_function(kernel_path, "BeginCanonicalValidation")
        self.assertLess(
            begin.index("if (result != VALIDATE_OK)"),
            begin.index("portable ="),
        )
        samples = source_function(kernel_path, "ContinueCanonicalValidation")
        for required in (
            "ReadBe16(sample + 2)",
            "ReadBeS24(sample + 4)",
            "ReadBeS24(sample + 7)",
            "ReadBeS24(sample + 10)",
            "ReadBe24(sample + 13)",
            "SUSAMUNE_GHOST_ANIMATION_RESERVED_MASK",
            "SUSAMUNE_GHOST_ANIMATION_ID_MAX",
        ):
            self.assertIn(required, samples)
        sanitize = source_function(ppc_path, "sanitizeSlot")
        for required in (
            "SUSAMUNE_GHOST_FILE_VERSION_V3",
            "SUSAMUNE_GHOST_FILE_VERSION_V4",
            "SUSAMUNE_GHOST_RECORDING_POSE_QF",
            "SUSAMUNE_GHOST_V4_SAMPLE_DATA_OFFSET",
            "SUSAMUNE_GHOST_POSE_SAMPLE_SIZE",
            "SUSAMUNE_GHOST_CODEC_RAW",
            "SUSAMUNE_GHOST_CODEC_POSE_ATTACHMENTS",
        ):
            self.assertIn(required, sanitize)
        self.assertNotIn("SUSAMUNE_GHOST_FILE_VERSION_V1", sanitize)
        self.assertNotIn("SUSAMUNE_GHOST_FILE_VERSION_V2", sanitize)

    def test_ppc_catalog_names_never_render_blank(self) -> None:
        root = Path(__file__).resolve().parents[1]
        ppc = root / "src/ghost_storage.cpp"
        visible = source_function(ppc, "copyVisibleName")
        self.assertIn("text[i] != ' '", visible)
        self.assertIn("kUnnamedName", visible)
        catalog_name = source_function(ppc, "copyCatalogName")
        self.assertIn("copyVisibleName", catalog_name)
        for function in ("copySlotName", "copyImportedSlotName"):
            self.assertIn(
                "copyCatalogName", source_function(ppc, function)
            )

    def test_race_load_starts_with_a_clean_challenger(self) -> None:
        root = Path(__file__).resolve().parents[1]
        ghost_path = root / "src/ghost.cpp"
        storage_path = root / "src/ghost_storage.cpp"
        source = ghost_path.read_text(encoding="utf-8")
        race_load = source_function(ghost_path, "importPlayback")

        # An asynchronous target can arrive after the player has already left
        # the menu. That prefix was recorded without its opponent and must not
        # be retained as the challenger on the first restart. A finished run
        # remains available for an explicit save.
        self.assertIn(
            "if (sRecording || !sRecord.valid) clearRecord();", race_load
        )
        self.assertIn("sRecording = false", race_load)
        self.assertIn("sAttemptSerial = gQFTTimer.attemptSerial()", race_load)
        self.assertIn("sBoundaryPending = false", race_load)
        self.assertIn("sPlaybackPinned = true", race_load)
        self.assertIn("sGhostVisible = false", race_load)
        self.assertLess(
            race_load.index("installCanonicalTrack(sPlayback"),
            race_load.index("clearRecord()"),
        )

        complete = source_function(storage_path, "completeRequest")
        self.assertIn("notify(kRaceLoaded)", complete)
        self.assertIn('"Ghost ready - restart to race"',
                      storage_path.read_text(encoding="utf-8"))

        # Once the clean attempt completes, it remains the save source while
        # the selected opponent stays pinned. Storage acknowledgement releases
        # it for a later race without touching the opponent.
        begin_attempt = source_function(ghost_path, "beginAttempt")
        self.assertIn(
            "recordPromotable && sPlaybackPinned && !sRecord.saved",
            begin_attempt,
        )

        selection = re.search(
            r"SaveSelection latestSaveableTrack\(\) \{(?P<body>.*?)\n\}",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(selection)
        body = selection.group("body")
        self.assertIn("sPlayback.valid && !sPlaybackPinned", body)
        self.assertLess(
            body.index("if (sRecord.valid"),
            body.index("sPlayback.valid && !sPlaybackPinned"),
        )

    def test_quarantined_import_stays_visible_and_deletable(self) -> None:
        root = Path(__file__).resolve().parents[1]
        kernel = root / "launcher/kernel/SusamuneGhost.c"
        ppc = root / "src/ghost_storage.cpp"
        quarantine = source_function(kernel, "QuarantineImportedSlot")
        for flag in ("SUSAMUNE_GHOST_SLOT_PRESENT",
                     "SUSAMUNE_GHOST_SLOT_IMPORTED",
                     "SUSAMUNE_GHOST_SLOT_UNSAFE"):
            self.assertIn(flag, quarantine)
        sanitize = source_function(ppc, "sanitizeSlot")
        self.assertIn("out->flags |= SUSAMUNE_GHOST_SLOT_PRESENT", sanitize)
        remove = source_function(ppc, "removeImported")
        self.assertIn("slot, true, true", remove)
        load_close = source_function(kernel, "LoadClosePass")
        self.assertIn("ImportedCatalogReady = false;", load_close)
        self.assertNotIn("QuarantineImportedSlot", load_close)

    def test_route_tuple_mismatch_is_rejected(self) -> None:
        no_internal_flag = envelope(build_ghost(
            route_parent_area=2,
            route_flags=0,
        ))
        with self.assertRaisesRegex(storage.StorageError, "internal-scene"):
            storage.validate_slot_file(
                no_internal_flag,
                game_id=ghost_format.REGION_GAME_IDS[0], profile=0, slot=0,
            )

        parent_start_without_parent = envelope(build_ghost(
            route_parent_area=ghost_format.ROUTE_PARENT_NONE,
            route_flags=ghost_format.ROUTE_PARENT_START,
        ))
        with self.assertRaisesRegex(storage.StorageError, "parent-start"):
            storage.validate_slot_file(
                parent_start_without_parent,
                game_id=ghost_format.REGION_GAME_IDS[0], profile=0, slot=0,
            )

    def test_share_paths_are_bounded_and_friendly(self) -> None:
        self.assertEqual(
            storage.import_directory(),
            "/susamune_ghosts/import",
        )

        raw = build_ghost(route_episode=3)
        ghost = storage.validate_share_file(
            raw, game_id=ghost_format.REGION_GAME_IDS[0], profile=0,
        )
        compact = storage.compact_milliseconds(
            storage.duration_milliseconds(ghost["duration_qf"])
        )
        export_path = storage.export_share_path(
            "jp", 0, ghost, date(2026, 8, 15)
        )
        self.assertEqual(
            export_path,
            "/susamune_ghosts/share/jp/p0/"
            f"2026_08_15_BH4_{compact}"
            f"[{ghost['file_checksum']:08X}].smsghost",
        )
        self.assertEqual(
            export_path,
            storage.export_share_path("jp", 0, ghost, date(2026, 8, 15)),
        )
        self.assertNotEqual(
            export_path,
            storage.export_share_path("jp", 0, ghost, date(2026, 8, 16)),
        )
        self.assertNotIn("Test Ghost", storage.export_share_path(
            "jp", 0, ghost, date(2026, 8, 15)
        ))
        self.assertEqual(storage.compact_milliseconds(42_490), "42490")
        self.assertEqual(storage.compact_milliseconds(62_345), "102345")
        self.assertEqual(storage.duration_milliseconds(120), 1001)

        internal = storage.validate_share_file(
            build_ghost(
                route_area=0x37,
                route_parent_area=2,
                route_flags=ghost_format.ROUTE_INTERNAL_SCENE,
                route_variant=3,
            ),
            game_id=ghost_format.REGION_GAME_IDS[0], profile=0,
        )
        self.assertEqual(storage.route_filename_label(internal), "BH4")
        fallback = storage.validate_share_file(
            build_ghost(route_area=0x0A, route_episode=9),
            game_id=ghost_format.REGION_GAME_IDS[0], profile=0,
        )
        self.assertEqual(storage.route_filename_label(fallback), "A0AE09")

        with self.assertRaises(storage.StorageError):
            storage.share_directory("pal", 4)
        with self.assertRaises(storage.StorageError):
            storage.share_directory("../../pal", 0)

    def test_share_round_trip_is_raw_canonical(self) -> None:
        raw = build_ghost()
        parsed = storage.validate_share_file(
            raw, game_id=ghost_format.REGION_GAME_IDS[0], profile=0,
        )
        self.assertEqual(parsed["file_size"], len(raw))

        interrupted = bytes(ghost_format.GHOST_HEADER_SIZE) + raw[
            ghost_format.GHOST_HEADER_SIZE:
        ]
        with self.assertRaises(storage.StorageError):
            storage.validate_share_file(
                interrupted,
                game_id=ghost_format.REGION_GAME_IDS[0], profile=0,
            )

    def test_export_checks_region_and_profile(self) -> None:
        raw = build_ghost()
        with self.assertRaisesRegex(storage.StorageError, "game id mismatch"):
            storage.validate_share_file(
                raw, game_id=ghost_format.REGION_GAME_IDS[1], profile=0,
            )
        with self.assertRaisesRegex(storage.StorageError, "profile mismatch"):
            storage.validate_share_file(
                raw, game_id=ghost_format.REGION_GAME_IDS[0], profile=1,
            )

    def test_global_import_pool_is_sorted_bounded_and_path_safe(self) -> None:
        leaves = [f"ghost_{number:02d}.smsghost" for number in range(14)]
        selected, overflow = storage.sorted_import_leaves(
            list(reversed(leaves)) + ["ignored.txt", "../bad.smsghost"]
        )
        self.assertEqual(selected, leaves[:12])
        self.assertEqual(overflow, 2)
        case_sorted, case_overflow = storage.sorted_import_leaves([
            "a.smsghost", "A.smsghost",
        ])
        self.assertEqual(case_sorted, ["A.smsghost", "a.smsghost"])
        self.assertEqual(case_overflow, 0)
        self.assertEqual(
            len(storage.validate_import_leaf(
                "g" * (storage.IMPORT_LEAF_SIZE -
                       len(storage.SHARE_EXTENSION) - 1) +
                storage.SHARE_EXTENSION
            )),
            storage.IMPORT_LEAF_SIZE - 1,
        )
        self.assertEqual(
            storage.PROFILE_WRITABLE_ENTRIES * 4 +
            storage.IMPORTED_MAX_ENTRIES,
            ghost_format.PROFILE_MAX_ENTRIES * 4,
        )
        for invalid in (
            "../ghost.smsghost",
            "sub/ghost.smsghost",
            "ghost.smsghost.",
            "ghost.txt",
            "g\N{SNOWMAN}.smsghost",
            "g" * storage.IMPORT_LEAF_SIZE + ".smsghost",
        ):
            with self.subTest(invalid=invalid):
                with self.assertRaises(storage.StorageError):
                    storage.validate_import_leaf(invalid)

    def test_import_accepts_only_explicit_portable_cross_region_routes(self) -> None:
        portable_pal = build_ghost(
            region=2,
            source_profile=3,
            route_area=0x2F,
            route_parent_area=2,
            route_flags=ghost_format.ROUTE_INTERNAL_SCENE,
            route_variant=2,
        )
        summary = storage.validate_import_file(portable_pal, running_region=0)
        self.assertEqual(summary["source_profile"], 3)
        self.assertEqual(summary["region"], "pal")

        portable_jp = build_ghost(region=0, route_area=0x02)
        self.assertEqual(
            storage.validate_import_file(
                portable_jp, running_region=2
            )["region"],
            "jp",
        )

        unsafe_pal = build_ghost(region=2, route_area=0x0A)
        with self.assertRaisesRegex(storage.StorageError,
                                    "no cross-region meaning"):
            storage.validate_import_file(unsafe_pal, running_region=0)

        selected, overflow = storage.select_import_files([
            ("z.smsghost", unsafe_pal),
            ("b.smsghost", portable_pal),
            ("a.smsghost", bytes(len(portable_pal))),
            ("c.smsghost", portable_jp),
        ], running_region=0)
        self.assertEqual([leaf for leaf, _ghost in selected], [
            "b.smsghost", "c.smsghost",
        ])
        self.assertEqual(overflow, 0)


if __name__ == "__main__":
    unittest.main()
