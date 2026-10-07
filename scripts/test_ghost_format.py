#!/usr/bin/env python3
"""Host tests for the canonical Susamune ghost format validator."""

from __future__ import annotations

from pathlib import Path
import re
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import validate_ghost as ghost_format


_AUTO = object()


def _write_text(
    target: bytearray, offset: int, capacity: int, value: str | bytes
) -> int:
    encoded = value.encode("ascii") if isinstance(value, str) else value
    if len(encoded) > capacity:
        raise ValueError("test text exceeds field")
    target[offset : offset + len(encoded)] = encoded
    return len(encoded)


def _finish_ghost(header: bytearray, payload: bytes) -> bytes:
    struct.pack_into(">I", header, 12, 0)
    struct.pack_into(">I", header, 16, 0)
    struct.pack_into(
        ">I", header, 16,
        ghost_format._crc32_zeroed(header, (12, 20)),
    )
    result = bytearray(header + payload)
    struct.pack_into(
        ">I", result, 12,
        ghost_format._crc32_zeroed(result, (12, 16)),
    )
    return bytes(result)


def _repair_ghost_checksums(raw: bytearray) -> bytes:
    struct.pack_into(">I", raw, 20, ghost_format._crc32(
        raw[ghost_format.GHOST_HEADER_SIZE:]
    ))
    struct.pack_into(">I", raw, 12, 0)
    struct.pack_into(">I", raw, 16, 0)
    struct.pack_into(">I", raw, 16, ghost_format._crc32_zeroed(
        raw[:ghost_format.GHOST_HEADER_SIZE], (12, 20)
    ))
    struct.pack_into(">I", raw, 12,
                     ghost_format._crc32_zeroed(raw, (12, 16)))
    return bytes(raw)


def build_ghost(
    *,
    ghost_id: int = 1,
    version: int = ghost_format.GHOST_VERSION,
    required_features: int | object = _AUTO,
    checksum_kind: int = ghost_format.CHECKSUM_CRC32,
    samples: list[tuple[int, ...]] | None = None,
    start_qf: int = 100,
    end_qf: int | None = None,
    duration_qf: int | None = None,
    result_qf: int | object = _AUTO,
    name: str | bytes = "Test Ghost",
    author: str = "Runner",
    profile_name: str = "Profile 1",
    route_area: int = 2,
    route_episode: int = 0,
    route_parent_area: int = ghost_format.ROUTE_PARENT_NONE,
    route_flags: int = 0,
    route_variant: int = 0,
    region: int = 0,
    source_profile: int = 0,
    run_flags: int = 0,
    reserved_byte: int = 0,
    segments: list[dict] | None = None,
    attachments: list[tuple[int, int]] | None = None,
    attachment_flags: int = 0,
) -> bytes:
    if samples is None:
        samples = [
            (160, 320, 480, 0x1000, 0, 0xC3, 0),
            (176, 320, 480, 0x1100, 4, 0xC3, 512),
        ]
    elapsed = sum(sample[4] for sample in samples[1:])
    if end_qf is None:
        end_qf = start_qf + elapsed
    if duration_qf is None:
        duration_qf = end_qf - start_qf
    if result_qf is _AUTO:
        result_qf = end_qf

    normalized_samples = [
        sample if len(sample) in (7, 9) else (*sample, 0xC3, 0)
        for sample in samples
    ]
    if version == ghost_format.GHOST_VERSION_V4:
        sample_data = b"".join(
            ghost_format.pack_pose_sample_v4(
                *sample[:6],
                round(sample[6] * ghost_format.V4_ANIMATION_PHASE_MAX /
                      ghost_format.ANIMATION_PHASE_MAX),
                *(sample[7:] if len(sample) >= 9 else (0, 0)),
            )
            for sample in normalized_samples
        )
    else:
        sample_data = b"".join(
            ghost_format.pack_pose_sample(*sample[:7])
            for sample in normalized_samples
        )
    header = bytearray(ghost_format.GHOST_HEADER_SIZE)
    author_length = _write_text(header, 96, ghost_format.AUTHOR_SIZE, author)
    name_length = _write_text(header, 120, ghost_format.NAME_SIZE, name)
    profile_length = _write_text(
        header, 168, ghost_format.PROFILE_NAME_SIZE, profile_name
    )
    if version in (
        ghost_format.GHOST_VERSION_V1,
        ghost_format.GHOST_VERSION_V2,
        ghost_format.GHOST_VERSION_V3,
        ghost_format.GHOST_VERSION_V4,
    ):
        if segments is None:
            segments = [{
                "first_sample": 0,
                "sample_count": len(samples),
                "start_qf": start_qf,
                "end_qf": end_qf,
                "route": {
                    "area": route_area,
                    "episode": route_episode,
                    "parent_area": route_parent_area,
                    "flags": route_flags,
                    "variant": route_variant,
                },
            }]
        table = bytearray(ghost_format.V3_SEGMENT_TABLE_SIZE)
        for index, segment in enumerate(segments):
            route = segment["route"]
            ghost_format._SEGMENT.pack_into(
                table,
                index * ghost_format.V3_SEGMENT_SIZE,
                segment["first_sample"],
                segment["sample_count"],
                segment["start_qf"],
                segment["end_qf"],
                route["variant"],
                route["area"],
                route["episode"],
                route["parent_area"],
                route["flags"],
                0,
                0,
            )
        if version == ghost_format.GHOST_VERSION_V4:
            descriptors = list(attachments or [])
            if len(descriptors) > ghost_format.V4_ATTACHMENT_DESCRIPTOR_COUNT:
                raise ValueError("too many test attachment descriptors")
            descriptors.extend(
                [(0, 0)] *
                (ghost_format.V4_ATTACHMENT_DESCRIPTOR_COUNT - len(descriptors))
            )
            descriptor_values = [value for pair in descriptors for value in pair]
            ghost_format._V4_EXTENSION.pack_into(
                header, 184, len(segments), ghost_format.V4_SEGMENT_SIZE,
                ghost_format.V4_SEGMENT_TABLE_OFFSET,
                ghost_format.V4_SEGMENT_TABLE_SIZE,
                ghost_format.V4_SAMPLE_DATA_OFFSET, len(sample_data),
                ghost_format._crc32(table), len(attachments or []),
                ghost_format.V4_ATTACHMENT_DESCRIPTOR_SIZE, attachment_flags,
                0, *descriptor_values,
            )
        else:
            ghost_format._V3_EXTENSION.pack_into(
                header, 184, len(segments), ghost_format.V3_SEGMENT_SIZE,
                ghost_format.V3_SEGMENT_TABLE_OFFSET,
                ghost_format.V3_SEGMENT_TABLE_SIZE,
                ghost_format.V3_SAMPLE_DATA_OFFSET, len(sample_data),
                ghost_format._crc32(table), *([0] * 12),
            )
        if reserved_byte:
            header[-1] = reserved_byte
        payload = bytes(table) + sample_data
    else:
        payload = sample_data
        if reserved_byte:
            header[184] = reserved_byte

    if required_features is _AUTO:
        required_features = (ghost_format.REQUIRED_EXTENDED_CODEC
                             if version == ghost_format.GHOST_VERSION_V4 else 0)
    ghost_format._GHOST_PREFIX.pack_into(
        header,
        0,
        ghost_format.GHOST_MAGIC,
        version,
        ghost_format.GHOST_HEADER_SIZE,
        ghost_format.GHOST_HEADER_SIZE + len(payload),
        0,
        0,
        ghost_format._crc32(payload),
        required_features,
        run_flags,
        ghost_format.REGION_GAME_IDS[region],
        0,
        region,
        source_profile,
        ghost_format.RECORDING_POSE_QF,
        (ghost_format.CODEC_POSE_ATTACHMENTS
         if version == ghost_format.GHOST_VERSION_V4 else
         ghost_format.CODEC_RAW),
        ghost_format.SAMPLE_SIZE,
        ghost_format.SAMPLE_INTERVAL_QF,
        route_area,
        route_episode,
        route_parent_area,
        route_flags,
        route_variant,
        result_qf,
        start_qf,
        end_qf,
        duration_qf,
        len(samples),
        len(payload),
        0,
        1_700_000_000,
        ghost_id >> 32,
        ghost_id & 0xFFFFFFFF,
        author_length,
        name_length,
        profile_length,
        checksum_kind,
    )
    return _finish_ghost(header, payload)


def ghost_entry(raw: bytes, *, flags: int = 0) -> dict:
    summary = ghost_format.validate_ghost(raw)
    return {
        "ghost_id": int(summary["ghost_id"], 16),
        "file_size": summary["file_size"],
        "file_checksum": summary["file_checksum"],
        "duration_qf": summary["duration_qf"],
        "result_qf": (ghost_format.RESULT_QF_NONE
                       if summary["result_qf"] is None
                       else summary["result_qf"]),
        "start_qf": summary["start_qf"],
        "end_qf": summary["end_qf"],
        "sample_count": summary["sample_count"],
        "created_unix": summary["created_unix"],
        "route": summary["route"],
        "name": summary["name"],
        "author": summary["author"],
        "flags": flags,
    }


def synthetic_entry(ghost_id: int, duration_qf: int = 4) -> dict:
    sample_count = duration_qf // ghost_format.SAMPLE_INTERVAL_QF + 1
    start_qf = 100
    return {
        "ghost_id": ghost_id,
        "file_size": ghost_format.V3_SAMPLE_DATA_OFFSET +
                     sample_count * ghost_format.SAMPLE_SIZE,
        "file_checksum": ghost_id & 0xFFFFFFFF,
        "duration_qf": duration_qf,
        "result_qf": start_qf + duration_qf,
        "start_qf": start_qf,
        "end_qf": start_qf + duration_qf,
        "sample_count": sample_count,
        "created_unix": 1_700_000_000 + ghost_id,
        "route": {
            "area": 2,
            "episode": 0,
            "parent_area": ghost_format.ROUTE_PARENT_NONE,
            "flags": 0,
            "variant": 0,
        },
        "name": f"Ghost {ghost_id}",
        "author": "Runner",
        "flags": 0,
    }


def build_index(
    entries: list[dict],
    *,
    version: int = 1,
    generation: int = 1,
    total_duration_qf: int | None = None,
    profile_name: str = "Profile 1",
) -> bytes:
    encoded_entries = bytearray()
    for entry in entries:
        encoded = bytearray(ghost_format.INDEX_ENTRY_SIZE)
        name_length = _write_text(
            encoded, 56, ghost_format.NAME_SIZE, entry["name"]
        )
        author_length = _write_text(
            encoded, 104, ghost_format.AUTHOR_SIZE, entry["author"]
        )
        route = entry["route"]
        created = entry["created_unix"]
        ghost_id = entry["ghost_id"]
        ghost_format._INDEX_ENTRY_PREFIX.pack_into(
            encoded,
            0,
            ghost_id >> 32,
            ghost_id & 0xFFFFFFFF,
            entry["file_size"],
            entry["file_checksum"],
            entry["duration_qf"],
            entry["result_qf"],
            entry["start_qf"],
            entry["end_qf"],
            entry["sample_count"],
            created >> 32,
            created & 0xFFFFFFFF,
            route["variant"],
            route["area"],
            route["episode"],
            route["parent_area"],
            route["flags"],
            name_length,
            author_length,
            entry["flags"],
        )
        encoded_entries.extend(encoded)

    if total_duration_qf is None:
        total_duration_qf = sum(entry["duration_qf"] for entry in entries)
    header = bytearray(ghost_format.INDEX_HEADER_SIZE)
    profile_length = _write_text(
        header, 56, ghost_format.PROFILE_NAME_SIZE, profile_name
    )
    file_size = ghost_format.INDEX_HEADER_SIZE + len(encoded_entries)
    ghost_format._INDEX_PREFIX.pack_into(
        header,
        0,
        ghost_format.INDEX_MAGIC,
        version,
        ghost_format.INDEX_HEADER_SIZE,
        ghost_format.INDEX_ENTRY_SIZE,
        len(entries),
        file_size,
        generation,
        0,
        ghost_format._crc32(encoded_entries),
        ghost_format.REGION_GAME_IDS[0],
        0,
        0,
        ghost_format.PROFILE_MAX_ENTRIES,
        0,
        total_duration_qf,
        ghost_format.PROFILE_MAX_DURATION_QF,
        0,
        1_700_000_000,
        profile_length,
        ghost_format.CHECKSUM_CRC32,
        0,
        0,
    )
    result = bytearray(header + encoded_entries)
    struct.pack_into(
        ">I", result, 20,
        ghost_format._crc32_zeroed(result, (20, 24)),
    )
    return bytes(result)


class GhostFileTests(unittest.TestCase):
    def test_valid_file_uses_explicit_qft_boundaries(self) -> None:
        summary = ghost_format.validate_ghost(build_ghost(start_qf=123))
        self.assertEqual(summary["start_qf"], 123)
        self.assertEqual(summary["end_qf"], 127)
        self.assertEqual(summary["duration_qf"], 4)
        self.assertEqual(
            summary["portable_filename"], "g_0000000000000001.smsghost"
        )

    def test_v3_pose_sample_has_exact_big_endian_offsets(self) -> None:
        packed = ghost_format.pack_pose_sample(
            -8_000_000, 0x010203, -1, -0x1234, 0x4567, 335, 4095
        )
        self.assertEqual(len(packed), 16)
        self.assertEqual(packed[0:2], b"\xed\xcc")
        self.assertEqual(packed[2:4], b"\x45\x67")
        self.assertEqual(packed[4:7], b"\x85\xee\x00")
        self.assertEqual(packed[7:10], b"\x01\x02\x03")
        self.assertEqual(packed[10:13], b"\xff\xff\xff")
        self.assertEqual(packed[13:16], b"\xa7\xff\xf8")
        self.assertEqual(
            ghost_format._unpack_pose_sample(packed, 0),
            (-8_000_000, 0x010203, -1, -0x1234, 0x4567,
             335, 4095, 0),
        )

    def test_v4_attachment_wire_layout_is_exact_and_big_endian(self) -> None:
        raw = build_ghost(
            samples=[
                (160, 320, 480, 0x1000, 0, 0xC3, 0, 1, 1),
                (176, 320, 480, 0x1100, 4, 0xC3, 4095, 4, 1),
            ],
            attachments=[(0x81234567, 0xABCD)],
        )
        self.assertEqual(raw[4:6], b"\x00\x04")
        self.assertEqual(raw[208:214], b"\x01\x06\x00\x00\x00\x00")
        self.assertEqual(raw[214:220], b"\x81\x23\x45\x67\xab\xcd")
        self.assertEqual(raw[220:256], bytes(36))
        first_pose = int.from_bytes(
            raw[ghost_format.V4_SAMPLE_DATA_OFFSET + 13:
                ghost_format.V4_SAMPLE_DATA_OFFSET + 16], "big"
        )
        self.assertEqual(first_pose & 0x7F, 0x11)
        summary = ghost_format.validate_ghost(raw)
        self.assertEqual(summary["version"], ghost_format.GHOST_VERSION_V4)
        self.assertEqual(summary["attachment_descriptors"], [{
            "object_id": "81234567", "name_key": "abcd",
        }])

    def test_v4_descriptor_header_is_strict(self) -> None:
        mutations = [
            (208, 8, "descriptor count"),
            (209, 5, "descriptor size"),
            (211, 2, "attachment flags"),
            (213, 1, "reserved"),
        ]
        for offset, value, message in mutations:
            with self.subTest(offset=offset):
                raw = bytearray(build_ghost())
                raw[offset] = value
                with self.assertRaisesRegex(ghost_format.FormatError, message):
                    ghost_format.validate_ghost(_repair_ghost_checksums(raw))

        with self.assertRaisesRegex(ghost_format.FormatError, "is empty"):
            ghost_format.validate_ghost(build_ghost(attachments=[(0, 0)]))
        with self.assertRaisesRegex(ghost_format.FormatError, "duplicated"):
            ghost_format.validate_ghost(build_ghost(
                attachments=[(0x10000001, 0x1234), (0x10000001, 0x1234)]
            ))
        raw = bytearray(build_ghost())
        raw[214] = 1
        with self.assertRaisesRegex(ghost_format.FormatError, "unused"):
            ghost_format.validate_ghost(_repair_ghost_checksums(raw))

    def test_v4_sample_attachment_indices_fail_closed(self) -> None:
        pose_offset = ghost_format.V4_SAMPLE_DATA_OFFSET + 13
        for yoshi in (6, 7):
            raw = bytearray(build_ghost())
            pose = int.from_bytes(raw[pose_offset:pose_offset + 3], "big")
            pose = (pose & ~0x70) | (yoshi << 4)
            raw[pose_offset:pose_offset + 3] = pose.to_bytes(3, "big")
            with self.subTest(yoshi=yoshi), self.assertRaisesRegex(
                ghost_format.FormatError, "Yoshi state"
            ):
                ghost_format.validate_ghost(_repair_ghost_checksums(raw))

        for held in range(8, 15):
            raw = bytearray(build_ghost())
            pose = int.from_bytes(raw[pose_offset:pose_offset + 3], "big")
            raw[pose_offset:pose_offset + 3] = (
                (pose & ~0xF) | held
            ).to_bytes(3, "big")
            with self.subTest(held=held), self.assertRaisesRegex(
                ghost_format.FormatError, "held descriptor"
            ):
                ghost_format.validate_ghost(_repair_ghost_checksums(raw))

        unknown = [(0, 0, 0, 0, 0, 0, 0, 0, 15),
                   (0, 0, 0, 0, 4, 0, 0, 0, 15)]
        with self.assertRaisesRegex(ghost_format.FormatError,
                                    "held descriptor"):
            ghost_format.validate_ghost(build_ghost(samples=unknown))
        summary = ghost_format.validate_ghost(build_ghost(
            samples=unknown,
            attachment_flags=ghost_format.V4_ATTACHMENT_HELD_OVERFLOW,
        ))
        self.assertEqual(
            summary["attachment_flags"],
            ghost_format.V4_ATTACHMENT_HELD_OVERFLOW,
        )

    def test_v3_and_v4_codec_contracts_are_not_interchangeable(self) -> None:
        ghost_format.validate_ghost(build_ghost(
            version=ghost_format.GHOST_VERSION_V3
        ))
        with self.assertRaisesRegex(ghost_format.FormatError,
                                    "lacks its required feature"):
            ghost_format.validate_ghost(build_ghost(required_features=0))
        raw = bytearray(build_ghost())
        raw[40] = ghost_format.CODEC_RAW
        with self.assertRaisesRegex(ghost_format.FormatError, "sample codec"):
            ghost_format.validate_ghost(_repair_ghost_checksums(raw))
        raw = bytearray(build_ghost(version=ghost_format.GHOST_VERSION_V3))
        raw[40] = ghost_format.CODEC_POSE_ATTACHMENTS
        with self.assertRaisesRegex(ghost_format.FormatError, "sample codec"):
            ghost_format.validate_ghost(_repair_ghost_checksums(raw))

    def test_v3_animation_id_and_reserved_bits_are_bounded(self) -> None:
        animation = ghost_format.V3_SAMPLE_DATA_OFFSET + 13
        raw = bytearray(build_ghost(version=ghost_format.GHOST_VERSION_V3))
        raw[animation:animation + 3] = (336 << 15).to_bytes(3, "big")
        with self.assertRaisesRegex(ghost_format.FormatError,
                                    "animation id"):
            ghost_format.validate_ghost(_repair_ghost_checksums(raw))

        raw = bytearray(build_ghost(version=ghost_format.GHOST_VERSION_V3))
        raw[animation + 2] |= 1
        with self.assertRaisesRegex(ghost_format.FormatError,
                                    "reserved bits"):
            ghost_format.validate_ghost(_repair_ghost_checksums(raw))

    def test_first_sample_must_be_normalized(self) -> None:
        raw = build_ghost(samples=[
            (0, 0, 0, 0, 5),
            (0, 0, 0, 0, 4),
        ])
        with self.assertRaisesRegex(ghost_format.FormatError, "first delta"):
            ghost_format.validate_ghost(raw)

    def test_duration_must_match_boundaries_and_deltas(self) -> None:
        with self.assertRaisesRegex(ghost_format.FormatError, "boundaries"):
            ghost_format.validate_ghost(build_ghost(duration_qf=5))
        raw = build_ghost(
            samples=[(0, 0, 0, 0, 0), (0, 0, 0, 0, 8)],
            end_qf=104,
        )
        with self.assertRaisesRegex(ghost_format.FormatError, "timeline"):
            ghost_format.validate_ghost(raw)

    def test_short_delta_is_only_for_a_segment_terminal_pose(self) -> None:
        terminal = [(0, 0, 0, 0, 0), (0, 0, 0, 0, 4), (0, 0, 0, 0, 2)]
        summary = ghost_format.validate_ghost(
            build_ghost(samples=terminal, start_qf=100)
        )
        self.assertEqual(summary["end_qf"], 106)

        nonterminal = [(0, 0, 0, 0, 0), (0, 0, 0, 0, 2), (0, 0, 0, 0, 4)]
        with self.assertRaisesRegex(ghost_format.FormatError, "short delta"):
            ghost_format.validate_ghost(build_ghost(samples=nonterminal))
        ghost_format.validate_ghost(build_ghost(
            samples=terminal,
            result_qf=ghost_format.RESULT_QF_NONE,
        ))
        ghost_format.validate_ghost(build_ghost(
            samples=terminal,
            run_flags=ghost_format.RUN_INCOMPLETE,
        ))

    def test_full_fifteen_minute_raw_file_fits(self) -> None:
        samples = [(0, 0, 0, 0, 0)]
        samples.extend(
            (0, 0, 0, 0, ghost_format.SAMPLE_INTERVAL_QF)
            for _ in range(ghost_format.MAX_SAMPLE_COUNT - 1)
        )
        raw = build_ghost(samples=samples, start_qf=0)
        summary = ghost_format.validate_ghost(raw)
        self.assertEqual(summary["duration_qf"], ghost_format.MAX_DURATION_QF)
        self.assertEqual(len(raw), ghost_format.V4_MAX_GHOST_FILE_SIZE)
        self.assertEqual(len(raw), 0x69EE0)
        self.assertLess(len(raw), 0x7FF00)

    def test_checksum_corruption_is_rejected(self) -> None:
        raw = bytearray(build_ghost())
        raw[-1] ^= 1
        with self.assertRaisesRegex(ghost_format.FormatError, "file checksum"):
            ghost_format.validate_ghost(bytes(raw))

    def test_host_reader_caps_input_before_full_read(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "oversized.smsghost"
            with path.open("wb") as stream:
                stream.write(ghost_format.GHOST_MAGIC)
                stream.seek(ghost_format.MAX_GHOST_FILE_SIZE)
                stream.write(b"\0")
            with self.assertRaisesRegex(ghost_format.FormatError, "size limit"):
                ghost_format._read_bounded(path)

    def test_forward_version_and_feature_are_distinct(self) -> None:
        for version in (ghost_format.GHOST_VERSION_V1,
                        ghost_format.GHOST_VERSION_V2, 7):
            with self.subTest(version=version), \
                 self.assertRaises(ghost_format.UnsupportedVersion):
                ghost_format.validate_ghost(build_ghost(version=version))
        with self.assertRaises(ghost_format.UnsupportedFeature):
            ghost_format.validate_ghost(build_ghost(required_features=2))

    def test_v3_segments_share_one_absolute_qft_timeline(self) -> None:
        samples = [
            (0, 0, 0, 0, 0),
            (16, 0, 0, 0, 4),
            (32, 0, 0, 0, 0),
            (48, 0, 0, 0, 4),
        ]
        segments = [
            {
                "first_sample": 0,
                "sample_count": 2,
                "start_qf": 100,
                "end_qf": 104,
                "route": {
                    "area": 2, "episode": 0,
                    "parent_area": ghost_format.ROUTE_PARENT_NONE,
                    "flags": 0, "variant": 0,
                },
            },
            {
                "first_sample": 2,
                "sample_count": 2,
                "start_qf": 120,
                "end_qf": 124,
                "route": {
                    "area": 0x0E, "episode": 0,
                    "parent_area": 2,
                    "flags": ghost_format.ROUTE_INTERNAL_SCENE,
                    "variant": 3,
                },
            },
        ]
        summary = ghost_format.validate_ghost(build_ghost(
            version=ghost_format.GHOST_VERSION_V3,
            samples=samples,
            start_qf=100,
            end_qf=124,
            duration_qf=24,
            result_qf=124,
            segments=segments,
        ))
        self.assertEqual(summary["segment_count"], 2)
        self.assertEqual(summary["duration_qf"], 24)
        self.assertEqual(summary["segments"][1]["start_qf"], 120)
        self.assertEqual(
            len(build_ghost(
                version=ghost_format.GHOST_VERSION_V3,
                samples=samples,
                start_qf=100,
                end_qf=124,
                duration_qf=24,
                result_qf=124,
                segments=segments,
            )),
            ghost_format.V3_SAMPLE_DATA_OFFSET +
            len(samples) * ghost_format.SAMPLE_SIZE,
        )

    def test_v3_allows_one_sample_segment_and_short_terminal(self) -> None:
        samples = [
            (0, 0, 0, 0, 0),
            (16, 0, 0, 0, 2),
            (32, 0, 0, 0, 0),
        ]
        segments = [
            {
                "first_sample": 0, "sample_count": 2,
                "start_qf": 100, "end_qf": 102,
                "route": {"area": 2, "episode": 0,
                          "parent_area": ghost_format.ROUTE_PARENT_NONE,
                          "flags": 0, "variant": 0},
            },
            {
                "first_sample": 2, "sample_count": 1,
                "start_qf": 110, "end_qf": 110,
                "route": {"area": 3, "episode": 0,
                          "parent_area": ghost_format.ROUTE_PARENT_NONE,
                          "flags": 0, "variant": 0},
            },
        ]
        summary = ghost_format.validate_ghost(build_ghost(
            version=ghost_format.GHOST_VERSION_V3,
            samples=samples, start_qf=100, end_qf=110,
            duration_qf=10, result_qf=110, segments=segments,
        ))
        self.assertEqual(summary["segments"][1]["sample_count"], 1)

    def test_v3_rejects_overlap_sample_holes_and_table_corruption(self) -> None:
        base = {
            "route": {"area": 2, "episode": 0,
                      "parent_area": ghost_format.ROUTE_PARENT_NONE,
                      "flags": 0, "variant": 0},
        }
        samples = [(0, 0, 0, 0, 0), (0, 0, 0, 0, 4),
                   (0, 0, 0, 0, 0), (0, 0, 0, 0, 4)]
        overlap = [
            {**base, "first_sample": 0, "sample_count": 2,
             "start_qf": 100, "end_qf": 104},
            {**base, "first_sample": 2, "sample_count": 2,
             "start_qf": 103, "end_qf": 107},
        ]
        with self.assertRaisesRegex(ghost_format.FormatError, "overlaps"):
            ghost_format.validate_ghost(build_ghost(
                version=3, samples=samples, start_qf=100, end_qf=107,
                duration_qf=7, result_qf=107, segments=overlap,
            ))

        hole = [
            {**base, "first_sample": 0, "sample_count": 2,
             "start_qf": 100, "end_qf": 104},
            {**base, "first_sample": 3, "sample_count": 1,
             "start_qf": 110, "end_qf": 110},
        ]
        with self.assertRaisesRegex(ghost_format.FormatError, "not contiguous"):
            ghost_format.validate_ghost(build_ghost(
                version=3, samples=samples, start_qf=100, end_qf=110,
                duration_qf=10, result_qf=110, segments=hole,
            ))

        raw = bytearray(build_ghost(version=3))
        raw[ghost_format.V3_SEGMENT_TABLE_OFFSET] ^= 1
        struct.pack_into(">I", raw, 20, ghost_format._crc32(
            raw[ghost_format.GHOST_HEADER_SIZE:]
        ))
        struct.pack_into(">I", raw, 12, 0)
        struct.pack_into(">I", raw, 16, 0)
        struct.pack_into(">I", raw, 16, ghost_format._crc32_zeroed(
            raw[:ghost_format.GHOST_HEADER_SIZE], (12, 20)
        ))
        struct.pack_into(">I", raw, 12,
                         ghost_format._crc32_zeroed(raw, (12, 16)))
        with self.assertRaisesRegex(ghost_format.FormatError, "table checksum"):
            ghost_format.validate_ghost(bytes(raw))

    def test_text_route_and_reserved_bytes_are_bounded(self) -> None:
        with self.assertRaisesRegex(ghost_format.FormatError, "printable ASCII"):
            ghost_format.validate_ghost(build_ghost(name=b"Caf\xe9"))
        with self.assertRaisesRegex(ghost_format.FormatError, "unsafe character"):
            ghost_format.validate_ghost(build_ghost(name="bad/name"))
        with self.assertRaisesRegex(ghost_format.FormatError, "route area"):
            ghost_format.validate_ghost(
                build_ghost(route_area=ghost_format.ROUTE_AREA_MAX + 1)
            )
        with self.assertRaisesRegex(ghost_format.FormatError, "reserved"):
            ghost_format.validate_ghost(build_ghost(
                version=ghost_format.GHOST_VERSION_V3, reserved_byte=1
            ))

    def test_route_parent_and_flags_must_agree(self) -> None:
        parent = ghost_format.validate_ghost(build_ghost(
            route_parent_area=2,
            route_flags=ghost_format.ROUTE_INTERNAL_SCENE,
        ))
        self.assertEqual(parent["route"]["parent_area"], 2)
        parent_start = ghost_format.validate_ghost(build_ghost(
            route_parent_area=2,
            route_flags=(ghost_format.ROUTE_INTERNAL_SCENE |
                         ghost_format.ROUTE_PARENT_START),
        ))
        self.assertEqual(
            parent_start["route"]["flags"],
            ghost_format.ROUTE_FLAGS_V1,
        )

        with self.assertRaisesRegex(ghost_format.FormatError, "internal-scene"):
            ghost_format.validate_ghost(build_ghost(route_parent_area=2))
        with self.assertRaisesRegex(ghost_format.FormatError, "internal-scene"):
            ghost_format.validate_ghost(
                build_ghost(route_flags=ghost_format.ROUTE_INTERNAL_SCENE)
            )
        with self.assertRaisesRegex(ghost_format.FormatError, "parent-start"):
            ghost_format.validate_ghost(
                build_ghost(route_flags=ghost_format.ROUTE_PARENT_START)
            )

    def test_result_is_absolute_qft_or_none(self) -> None:
        summary = ghost_format.validate_ghost(
            build_ghost(result_qf=ghost_format.RESULT_QF_NONE)
        )
        self.assertIsNone(summary["result_qf"])
        with self.assertRaisesRegex(ghost_format.FormatError, "result"):
            ghost_format.validate_ghost(build_ghost(result_qf=99))

    def test_cross_region_import_uses_canonical_route_table(self) -> None:
        portable = build_ghost(
            region=2,
            route_area=0x2A,
            route_parent_area=8,
            route_flags=ghost_format.ROUTE_INTERNAL_SCENE,
            route_variant=4,
        )
        parsed = ghost_format.validate_imported_ghost(
            portable, running_region=0
        )
        self.assertEqual(parsed["region"], "pal")

        wrong_parent = build_ghost(
            region=2,
            route_area=0x2A,
            route_parent_area=5,
            route_flags=ghost_format.ROUTE_INTERNAL_SCENE,
            route_variant=4,
        )
        with self.assertRaisesRegex(
            ghost_format.FormatError, "parent differs"
        ):
            ghost_format.validate_imported_ghost(
                wrong_parent, running_region=0
            )

        unknown_foreign = build_ghost(region=2, route_area=0x0A)
        with self.assertRaisesRegex(
            ghost_format.FormatError, "no cross-region meaning"
        ):
            ghost_format.validate_imported_ghost(
                unknown_foreign, running_region=0
            )
        ghost_format.validate_imported_ghost(
            build_ghost(route_area=0x0A), running_region=0
        )

        samples = [
            (0, 0, 0, 0, 0), (16, 0, 0, 0, 4),
            (32, 0, 0, 0, 0), (48, 0, 0, 0, 4),
        ]
        segments = [
            {
                "first_sample": 0, "sample_count": 2,
                "start_qf": 100, "end_qf": 104,
                "route": {
                    "area": 2, "episode": 0,
                    "parent_area": ghost_format.ROUTE_PARENT_NONE,
                    "flags": 0, "variant": 0,
                },
            },
            {
                "first_sample": 2, "sample_count": 2,
                "start_qf": 120, "end_qf": 124,
                "route": {
                    "area": 0x2F, "episode": 0, "parent_area": 2,
                    "flags": ghost_format.ROUTE_INTERNAL_SCENE,
                    "variant": 2,
                },
            },
        ]
        foreign_v3 = build_ghost(
            version=ghost_format.GHOST_VERSION_V3,
            region=2,
            samples=samples,
            start_qf=100,
            end_qf=124,
            duration_qf=24,
            result_qf=124,
            segments=segments,
        )
        ghost_format.validate_imported_ghost(foreign_v3, running_region=0)
        segments[1]["route"]["parent_area"] = 3
        with self.assertRaisesRegex(ghost_format.FormatError, "parent differs"):
            ghost_format.validate_imported_ghost(
                build_ghost(
                    version=ghost_format.GHOST_VERSION_V3,
                    region=2,
                    samples=samples,
                    start_qf=100,
                    end_qf=124,
                    duration_qf=24,
                    result_qf=124,
                    segments=segments,
                ),
                running_region=0,
            )

    def test_host_portable_table_matches_shared_header(self) -> None:
        header = (
            Path(__file__).resolve().parents[1] /
            "include" / "susamune" / "ghost_format.h"
        ).read_text(encoding="ascii")
        start = header.index("#define SUSAMUNE_GHOST_PORTABLE_ROUTE_LIST")
        end = header.index("\n\n#define SUSAMUNE_GHOST_AUTHOR_SIZE", start)
        pairs = {
            int(area, 16): int(parent, 16)
            for area, parent in re.findall(
                r"X\(0x([0-9A-Fa-f]+)u, 0x([0-9A-Fa-f]+)u\)",
                header[start:end],
            )
        }
        self.assertEqual(pairs, ghost_format.PORTABLE_ROUTE_PARENTS)


class PortableIndexTests(unittest.TestCase):
    def test_valid_index_and_bundle(self) -> None:
        raw = build_ghost(ghost_id=0x1234)
        index = build_index([ghost_entry(raw)], generation=9)
        summary = ghost_format.validate_index(index)
        self.assertEqual(summary["generation"], 9)
        self.assertEqual(summary["entry_count"], 1)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            filename = ghost_format.portable_ghost_filename(0x1234)
            (root / filename).write_bytes(raw)
            bundle = ghost_format.validate_bundle(index, root)
            self.assertEqual(bundle["bundle_directory"], str(root))

    def test_count_and_duration_are_independent_limits(self) -> None:
        forty_full = [
            synthetic_entry(index + 1, ghost_format.MAX_DURATION_QF)
            for index in range(40)
        ]
        summary = ghost_format.validate_index(build_index(forty_full))
        self.assertEqual(summary["total_duration_qf"], 4_315_680)

        forty_one_full = forty_full + [
            synthetic_entry(41, ghost_format.MAX_DURATION_QF)
        ]
        with self.assertRaisesRegex(ghost_format.FormatError, "10-hour"):
            ghost_format.validate_index(build_index(forty_one_full))

        forty_eight_short = [synthetic_entry(index + 1) for index in range(48)]
        self.assertEqual(
            ghost_format.validate_index(build_index(forty_eight_short))["entry_count"],
            48,
        )
        forty_nine_short = forty_eight_short + [synthetic_entry(49)]
        with self.assertRaisesRegex(ghost_format.FormatError, "48 ghosts"):
            ghost_format.validate_index(build_index(forty_nine_short))

    def test_duplicate_id_and_cached_total_are_rejected(self) -> None:
        entry = synthetic_entry(1)
        with self.assertRaisesRegex(ghost_format.FormatError, "duplicates"):
            ghost_format.validate_index(build_index([entry, entry]))
        with self.assertRaisesRegex(ghost_format.FormatError, "total duration"):
            ghost_format.validate_index(
                build_index([entry], total_duration_qf=5)
            )
        impossible = synthetic_entry(2)
        impossible["sample_count"] = ghost_format.MAX_SAMPLE_COUNT + 1
        impossible["file_size"] = (
            ghost_format.V3_SAMPLE_DATA_OFFSET +
            impossible["sample_count"] * ghost_format.SAMPLE_SIZE
        )
        with self.assertRaisesRegex(ghost_format.FormatError, "sample count"):
            ghost_format.validate_index(build_index([impossible]))

    def test_generation_selection_falls_back_from_corruption(self) -> None:
        old = build_index([synthetic_entry(1)], generation=7)
        corrupt_new = bytearray(build_index([synthetic_entry(2)], generation=8))
        corrupt_new[-1] ^= 1
        selected, summary = ghost_format.select_newest_index([
            ("a", old),
            ("b", bytes(corrupt_new)),
        ])
        self.assertEqual(selected, "a")
        self.assertEqual(summary["generation"], 7)

    def test_forward_index_blocks_stale_fallback(self) -> None:
        old = build_index([synthetic_entry(1)], generation=7)
        forward = build_index([synthetic_entry(2)], version=2, generation=8)
        with self.assertRaises(ghost_format.UnsupportedVersion):
            ghost_format.select_newest_index([("a", old), ("b", forward)])

    def test_wrapping_generation_order(self) -> None:
        self.assertTrue(ghost_format.generation_is_newer(0, 0xFFFFFFFF))
        self.assertFalse(ghost_format.generation_is_newer(0xFFFFFFFF, 0))
        a = build_index([synthetic_entry(1)], generation=0xFFFFFFFF)
        b = build_index([synthetic_entry(2)], generation=0)
        selected, _summary = ghost_format.select_newest_index([("a", a), ("b", b)])
        self.assertEqual(selected, "b")

    def test_portable_filename_never_uses_display_text(self) -> None:
        self.assertEqual(
            ghost_format.portable_ghost_filename(0xABC),
            "g_0000000000000abc.smsghost",
        )
        with self.assertRaises(ghost_format.FormatError):
            ghost_format.portable_ghost_filename(0)


if __name__ == "__main__":
    unittest.main()
