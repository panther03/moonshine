#!/usr/bin/env python3
"""Validate canonical Susamune V3/V4 ghosts and portable export bundles."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import sys
import zlib


GHOST_MAGIC = b"SGHF"
INDEX_MAGIC = b"SGIX"
VERSION = 1  # SGIX remains V1.
GHOST_VERSION_V1 = 1
GHOST_VERSION_V2 = 2
GHOST_VERSION_V3 = 3
GHOST_VERSION_V4 = 4
GHOST_VERSION_V5 = 5
GHOST_VERSION_V6 = 6
V6_MAX_INPUT_COUNT = 36_000
V6_MAX_DURATION_QF = 71_928
V6_MAX_SAMPLE_COUNT = 17_983
GHOST_VERSION = GHOST_VERSION_V4
GHOST_HEADER_SIZE = 0x100
INDEX_HEADER_SIZE = 0x80
INDEX_ENTRY_SIZE = 0x80
CHECKSUM_CRC32 = 1

REGION_GAME_IDS = {
    0: 0x474D534A,  # GMSJ
    1: 0x474D5345,  # GMSE
    2: 0x474D5350,  # GMSP
}
REGION_NAMES = {0: "jp", 1: "us", 2: "pal"}
PROFILE_COUNT = 4
# Legacy optional SGIX V1 bundle policy; not a console library limit.
PROFILE_MAX_ENTRIES = 48
PROFILE_MAX_DURATION_QF = 4_315_684
MAX_DURATION_QF = 107_892
QF_MAX = 0x7FFFFFFF
MIN_SAMPLE_COUNT = 2
MAX_SAMPLE_COUNT = 26_974
SAMPLE_SIZE = 16
SAMPLE_INTERVAL_QF = 4
POSITION_SCALE = 8
MAX_POSITION_FIXED = 8_000_000
ANIMATION_ID_MAX = 335
ANIMATION_PHASE_MAX = 4095
ANIMATION_RESERVED_MASK = 0x7
MAX_SAMPLE_DATA_SIZE = 0x695E0
V3_MAX_SEGMENTS = 64
V3_SEGMENT_SIZE = 0x20
V3_SEGMENT_TABLE_OFFSET = 0x100
V3_SEGMENT_TABLE_SIZE = 0x800
V3_SAMPLE_DATA_OFFSET = 0x900
V3_MAX_PAYLOAD_SIZE = 0x69DE0
V3_MAX_GHOST_FILE_SIZE = 0x69EE0
V4_MAX_SEGMENTS = V3_MAX_SEGMENTS
V4_SEGMENT_SIZE = V3_SEGMENT_SIZE
V4_SEGMENT_TABLE_OFFSET = V3_SEGMENT_TABLE_OFFSET
V4_SEGMENT_TABLE_SIZE = V3_SEGMENT_TABLE_SIZE
V4_SAMPLE_DATA_OFFSET = V3_SAMPLE_DATA_OFFSET
V4_MAX_PAYLOAD_SIZE = V3_MAX_PAYLOAD_SIZE
V4_MAX_GHOST_FILE_SIZE = V3_MAX_GHOST_FILE_SIZE
V4_ANIMATION_PHASE_MAX = 255
V4_ANIMATION_PHASE_SHIFT = 7
V4_YOSHI_SHIFT = 4
V4_YOSHI_MASK = 0x7
V4_YOSHI_UNKNOWN = 5
V4_HELD_INDEX_MASK = 0xF
V4_HELD_UNKNOWN = 0xF
V4_ATTACHMENT_DESCRIPTOR_COUNT = 7
V4_ATTACHMENT_DESCRIPTOR_SIZE = 6
V4_ATTACHMENT_HELD_OVERFLOW = 0x0001
V4_ATTACHMENT_FLAGS = V4_ATTACHMENT_HELD_OVERFLOW
MAX_PAYLOAD_SIZE = V4_MAX_PAYLOAD_SIZE
V5_MAX_INPUT_COUNT = 54_000
V5_MAX_SPLIT_COUNT = 8
V5_TEACHING_HEADER_SIZE = 32
V5_MAX_GHOST_FILE_SIZE = V4_MAX_GHOST_FILE_SIZE + 32 + 54_000 * 16 + V5_MAX_SPLIT_COUNT * 12
MAX_GHOST_FILE_SIZE = V5_MAX_GHOST_FILE_SIZE
MAX_INDEX_FILE_SIZE = 0x1880
RESULT_QF_NONE = 0xFFFFFFFF
RECORDING_POSE_QF = 2
CODEC_RAW = 0
CODEC_POSE_ATTACHMENTS = 1
REQUIRED_EXTENDED_CODEC = 0x00000001
SUPPORTED_REQUIRED_FEATURES_V3 = 0
SUPPORTED_REQUIRED_FEATURES_V4 = REQUIRED_EXTENDED_CODEC
SUPPORTED_REQUIRED_FEATURES = SUPPORTED_REQUIRED_FEATURES_V4
RUN_INCOMPLETE = 0x00000008
RUN_ASSISTED = 0x00000001
RUN_TAS = 0x00000020
ROUTE_FLAGS_V1 = 0x03
ROUTE_INTERNAL_SCENE = 0x01
ROUTE_PARENT_START = 0x02
ROUTE_PARENT_NONE = 0xFF
ROUTE_VARIANT_NONE = -1
ROUTE_AREA_MAX = 0x3C
ROUTE_EPISODE_MAX = 9
ROUTE_VARIANT_MAX = 255
PORTABLE_ROUTE_PARENTS = {
    0x00: 0xFF, 0x01: 0xFF, 0x02: 0xFF, 0x03: 0xFF,
    0x04: 0xFF, 0x05: 0xFF, 0x06: 0xFF, 0x07: 0x06,
    0x08: 0xFF, 0x09: 0xFF, 0x0D: 0x05, 0x0E: 0x06,
    0x10: 0x09, 0x14: 0xFF, 0x15: 0xFF, 0x16: 0xFF,
    0x17: 0xFF, 0x18: 0xFF, 0x1D: 0xFF, 0x1E: 0x03,
    0x1F: 0x09, 0x20: 0x04, 0x21: 0x04, 0x28: 0x06,
    0x29: 0x05, 0x2A: 0x08, 0x2C: 0x09, 0x2E: 0x02,
    0x2F: 0x02, 0x30: 0x03, 0x32: 0x05, 0x33: 0x06,
    0x34: 0xFF, 0x37: 0x02, 0x38: 0x06, 0x39: 0x09,
    0x3A: 0x05, 0x3B: 0x03, 0x3C: 0xFF,
}

AUTHOR_SIZE = 24
NAME_SIZE = 48
PROFILE_NAME_SIZE = 16

_GHOST_PREFIX = struct.Struct(
    ">4sHHIIIIIII6BH4BiIIIIIIIIII4B"
)
_INDEX_PREFIX = struct.Struct(">4sHHHHIIIII4BIIII4B")
_INDEX_ENTRY_PREFIX = struct.Struct(">IIIIIIIIIIIi6BH")
_SEGMENT = struct.Struct(">IIIIi4BII")
_V3_EXTENSION = struct.Struct(">HHIIIII12I")
_V4_EXTENSION = struct.Struct(">HHIIIIIBBHH" + "IH" * 7)


class FormatError(ValueError):
    """The bytes are corrupt or violate a bounded format invariant."""


class UnsupportedVersion(FormatError):
    """The magic is known, but this reader must not interpret the version."""


class UnsupportedFeature(FormatError):
    """The version is known, but required decoding semantics are unknown."""


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise FormatError(message)


def _crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def _crc32_zeroed(data: bytes, *ranges: tuple[int, int]) -> int:
    work = bytearray(data)
    for start, end in ranges:
        _require(0 <= start <= end <= len(work), "checksum field outside file")
        work[start:end] = bytes(end - start)
    return _crc32(work)


def _signed_be24(raw: bytes) -> int:
    _require(len(raw) == 3, "truncated signed BE24")
    value = int.from_bytes(raw, "big")
    return value - 0x1000000 if value & 0x800000 else value


def pack_pose_sample(
    x: int,
    y: int,
    z: int,
    yaw: int,
    delta_qf: int,
    animation_id: int,
    animation_phase: int,
) -> bytes:
    """Pack one V3 pose sample from fixed-point coordinates."""
    for value in (x, y, z):
        if not -(1 << 23) <= value < (1 << 23):
            raise ValueError("pose coordinate does not fit signed BE24")
    if not 0 <= animation_id <= ANIMATION_ID_MAX:
        raise ValueError("animation id outside V3 range")
    if not 0 <= animation_phase <= ANIMATION_PHASE_MAX:
        raise ValueError("animation phase outside V3 range")
    animation = (animation_id << 15) | (animation_phase << 3)
    coordinates = b"".join(
        (value & 0xFFFFFF).to_bytes(3, "big") for value in (x, y, z)
    )
    return struct.pack(">hH", yaw, delta_qf) + coordinates + \
        animation.to_bytes(3, "big")


def pack_pose_sample_v4(
    x: int,
    y: int,
    z: int,
    yaw: int,
    delta_qf: int,
    animation_id: int,
    animation_phase: int,
    yoshi: int = 0,
    held: int = 0,
) -> bytes:
    """Pack one V4 pose/attachment sample without changing its 16-byte stride."""
    for value in (x, y, z):
        if not -(1 << 23) <= value < (1 << 23):
            raise ValueError("pose coordinate does not fit signed BE24")
    if not 0 <= animation_id <= ANIMATION_ID_MAX:
        raise ValueError("animation id outside V4 range")
    if not 0 <= animation_phase <= V4_ANIMATION_PHASE_MAX:
        raise ValueError("animation phase outside V4 range")
    if not 0 <= yoshi <= V4_YOSHI_MASK:
        raise ValueError("Yoshi state outside V4 range")
    if not 0 <= held <= V4_HELD_INDEX_MASK:
        raise ValueError("held index outside V4 range")
    pose = ((animation_id << 15) |
            (animation_phase << V4_ANIMATION_PHASE_SHIFT) |
            (yoshi << V4_YOSHI_SHIFT) | held)
    coordinates = b"".join(
        (value & 0xFFFFFF).to_bytes(3, "big") for value in (x, y, z)
    )
    return struct.pack(">hH", yaw, delta_qf) + coordinates + \
        pose.to_bytes(3, "big")


def _unpack_pose_sample(data: bytes, offset: int) -> tuple[int, ...]:
    _require(0 <= offset and offset + SAMPLE_SIZE <= len(data),
             "truncated pose sample")
    yaw, delta_qf = struct.unpack_from(">hH", data, offset)
    x = _signed_be24(data[offset + 4:offset + 7])
    y = _signed_be24(data[offset + 7:offset + 10])
    z = _signed_be24(data[offset + 10:offset + 13])
    animation = int.from_bytes(data[offset + 13:offset + 16], "big")
    animation_id = animation >> 15
    animation_phase = (animation >> 3) & ANIMATION_PHASE_MAX
    reserved = animation & ANIMATION_RESERVED_MASK
    return x, y, z, yaw, delta_qf, animation_id, animation_phase, reserved


def _unpack_pose_sample_v4(data: bytes, offset: int) -> tuple[int, ...]:
    _require(0 <= offset and offset + SAMPLE_SIZE <= len(data),
             "truncated pose sample")
    yaw, delta_qf = struct.unpack_from(">hH", data, offset)
    x = _signed_be24(data[offset + 4:offset + 7])
    y = _signed_be24(data[offset + 7:offset + 10])
    z = _signed_be24(data[offset + 10:offset + 13])
    pose = int.from_bytes(data[offset + 13:offset + 16], "big")
    return (
        x, y, z, yaw, delta_qf, pose >> 15,
        (pose >> V4_ANIMATION_PHASE_SHIFT) & V4_ANIMATION_PHASE_MAX,
        (pose >> V4_YOSHI_SHIFT) & V4_YOSHI_MASK,
        pose & V4_HELD_INDEX_MASK,
    )


def _version(data: bytes, magic: bytes, label: str) -> int:
    _require(len(data) >= 8, f"truncated {label} prefix")
    _require(data[:4] == magic, f"bad {label} magic")
    version = struct.unpack_from(">H", data, 4)[0]
    if version != VERSION:
        raise UnsupportedVersion(
            f"unsupported {label} version {version}; reader supports {VERSION}"
        )
    return version


def _text(
    raw: bytes,
    offset: int,
    capacity: int,
    length: int,
    label: str,
    *,
    required: bool = False,
) -> str:
    _require(length <= capacity, f"{label} length exceeds {capacity}")
    field = raw[offset : offset + capacity]
    _require(len(field) == capacity, f"truncated {label}")
    _require(not any(field[length:]), f"nonzero bytes after {label}")
    if required:
        _require(length != 0, f"empty {label}")
    value_bytes = field[:length]
    _require(all(0x20 <= byte <= 0x7E for byte in value_bytes),
             f"{label} is not printable ASCII")
    value = value_bytes.decode("ascii")
    _require("/" not in value and "\\" not in value,
             f"unsafe character in {label}")
    return value


def _game_region(game_id: int, region: int, revision: int = 0) -> str:
    _require(region in REGION_GAME_IDS, f"invalid region {region}")
    _require(game_id == REGION_GAME_IDS[region], "game id does not match region")
    _require(revision == 0, f"unsupported disc revision {revision}")
    return REGION_NAMES[region]


def _validate_route(
    area: int,
    episode: int,
    parent_area: int,
    flags: int,
    variant: int,
    label: str,
) -> None:
    _require(area <= ROUTE_AREA_MAX, f"{label} area outside V1 bounds")
    _require(episode <= ROUTE_EPISODE_MAX,
             f"{label} episode outside V1 bounds")
    _require(parent_area == ROUTE_PARENT_NONE or parent_area <= ROUTE_AREA_MAX,
             f"{label} parent area outside V1 bounds")
    _require((flags & ~ROUTE_FLAGS_V1) == 0,
             f"{label} has unknown V1 flags")
    has_parent = parent_area != ROUTE_PARENT_NONE
    _require((flags & ROUTE_PARENT_START) == 0 or has_parent,
             f"{label} parent-start flag has no parent area")
    _require(bool(flags & ROUTE_INTERNAL_SCENE) == has_parent,
             f"{label} internal-scene flag disagrees with parent area")
    _require(ROUTE_VARIANT_NONE <= variant <= ROUTE_VARIANT_MAX,
             f"{label} variant outside V1 bounds")


def _validate_portable_route(route: dict, label: str) -> None:
    area = route["area"]
    _require(area in PORTABLE_ROUTE_PARENTS,
             f"{label} area has no cross-region meaning")
    expected_parent = PORTABLE_ROUTE_PARENTS[area]
    _require(route["parent_area"] == expected_parent,
             f"{label} parent differs across regions")
    if expected_parent == ROUTE_PARENT_NONE:
        _require(route["flags"] == 0,
                 f"{label} standalone route has internal flags")
    else:
        _require(route["flags"] & ROUTE_INTERNAL_SCENE,
                 f"{label} child route lacks internal flag")
        _require(not route["flags"] &
                 ~(ROUTE_INTERNAL_SCENE | ROUTE_PARENT_START),
                 f"{label} child route has unknown flags")


def portable_ghost_filename(ghost_id: int) -> str:
    """Return the host/export convention, never a console storage path."""
    _require(0 < ghost_id <= 0xFFFFFFFFFFFFFFFF, "ghost id must be nonzero u64")
    return f"g_{ghost_id:016x}.smsghost"


def _read_bounded(path: Path, expected_magic: bytes | None = None) -> bytes:
    try:
        with path.open("rb") as stream:
            prefix = stream.read(8)
            magic = prefix[:4]
            if expected_magic is not None:
                _require(magic == expected_magic,
                         f"{path.name} has unexpected file magic")
            if magic == GHOST_MAGIC:
                limit = MAX_GHOST_FILE_SIZE
            elif magic == INDEX_MAGIC:
                limit = MAX_INDEX_FILE_SIZE
            else:
                raise FormatError(f"{path.name} has unknown file magic")
            data = prefix + stream.read(limit - len(prefix) + 1)
    except OSError as error:
        raise FormatError(f"cannot read {path}: {error}") from error
    _require(len(data) <= limit, f"{path.name} exceeds its format size limit")
    return data


def _validate_teaching(data: bytes, start_qf: int, end_qf: int, ghost_version: int = 5) -> dict:
    _require(len(data) >= 32, "truncated teaching header")
    magic, version, header_size, inputs, splits, flags, checksum, r0, r1 = struct.unpack_from(
        ">IHH6I", data)
    fludd = ghost_version == 6
    stride = 24 if fludd else 16
    _require(magic == 0x53475449 and version == (2 if fludd else 1) and header_size == 32,
             "invalid teaching header")
    _require(inputs <= (V6_MAX_INPUT_COUNT if fludd else V5_MAX_INPUT_COUNT) and splits <= V5_MAX_SPLIT_COUNT,
             "teaching count exceeds bound")
    _require(not flags & ~1 and r0 == r1 == 0, "unknown teaching flags/reserved")
    _require(len(data) == 32 + inputs * stride + splits * 12, "teaching size mismatch")
    _require(_crc32(data[32:]) == checksum, "teaching checksum mismatch")
    previous = None
    for index in range(inputs):
        offset = 32 + index * stride
        qf, buttons = struct.unpack_from(">IH", data, offset)
        _require(start_qf <= qf <= end_qf and (previous is None or qf > previous),
                 "input timestamps outside timeline or unordered")
        _require(not buttons & ~0x1F7F and data[offset + 15] == 0,
                 "input reserved button/flag bits")
        if fludd:
            visual = data[offset + 16:offset + 24]
            mode = visual[0]
            pitch = ((visual[5] & 15) << 8) | visual[6]
            _require((not any(visual)) if not mode else
                     (not mode & ~0x1f and bool(mode & 8) and mode & 7 <= 5
                      and (bool(mode & 16) or visual[7] == 0)
                      and (pitch <= 1024 or pitch >= 3072)), "invalid FLUDD observation")
        previous = qf
    result = []
    previous = None
    identity = None
    for index in range(splits):
        qf, schema, route, endpoint, reserved = struct.unpack_from(
            ">IIHBB", data, 32 + inputs * stride + index * 12)
        _require(start_qf <= qf <= end_qf and (previous is None or qf >= previous),
                 "split timestamps outside timeline or unordered")
        _require(schema != 0 and route != 0xFFFF and endpoint == index and reserved == 0,
                 "invalid split identity/ordinal")
        _require(identity is None or identity == (schema, route), "mixed split schemas/routes")
        result.append({"qf": qf, "schema": schema, "route": route, "endpoint": endpoint})
        identity = (schema, route)
        previous = qf
    return {"input_count": inputs, "splits": result, "input_truncated": bool(flags & 1)}


def validate_ghost(data: bytes) -> dict:
    _require(len(data) >= 8, "truncated ghost prefix")
    _require(data[:4] == GHOST_MAGIC, "bad ghost magic")
    version = struct.unpack_from(">H", data, 4)[0]
    if version not in (GHOST_VERSION_V3, GHOST_VERSION_V4, GHOST_VERSION_V5, GHOST_VERSION_V6):
        raise UnsupportedVersion(
            f"unsupported ghost version {version}; reader supports 3, 4, 5 and 6"
        )
    _require(len(data) >= GHOST_HEADER_SIZE, "truncated ghost header")
    fields = _GHOST_PREFIX.unpack_from(data)
    (
        _magic,
        version,
        header_size,
        file_size,
        file_checksum,
        header_checksum,
        payload_checksum,
        required_features,
        run_flags,
        game_id,
        disc_revision,
        region,
        source_profile,
        recording_mode,
        sample_codec,
        sample_stride,
        sample_interval_qf,
        route_area,
        route_episode,
        route_parent_area,
        route_flags,
        route_variant,
        result_qf,
        start_qf,
        end_qf,
        duration_qf,
        sample_count,
        payload_size,
        created_hi,
        created_lo,
        ghost_id_hi,
        ghost_id_lo,
        author_length,
        name_length,
        profile_name_length,
        checksum_kind,
    ) = fields

    _require(header_size == GHOST_HEADER_SIZE, "invalid ghost header size")
    _require(file_size == len(data), "ghost file size does not match bytes")
    _require(file_size <= (V5_MAX_GHOST_FILE_SIZE if version >= 5 else V4_MAX_GHOST_FILE_SIZE),
             "ghost exceeds canonical file limit")
    if checksum_kind != CHECKSUM_CRC32:
        raise UnsupportedFeature(f"unsupported checksum kind {checksum_kind}")

    expected_header_crc = _crc32_zeroed(data[:header_size], (12, 20))
    _require(header_checksum == expected_header_crc, "ghost header checksum mismatch")
    expected_file_crc = _crc32_zeroed(data, (12, 16))
    _require(file_checksum == expected_file_crc, "ghost file checksum mismatch")

    supported_features = (7 if version == GHOST_VERSION_V6 else 3 if version == GHOST_VERSION_V5 else SUPPORTED_REQUIRED_FEATURES_V4
                          if version == GHOST_VERSION_V4
                          else SUPPORTED_REQUIRED_FEATURES_V3)
    if required_features & ~supported_features:
        raise UnsupportedFeature(
            f"unsupported required features {required_features:#010x}"
        )
    _require(version < GHOST_VERSION_V4 or
             required_features == supported_features,
             "V4 attachment codec lacks its required feature")
    _require(recording_mode == RECORDING_POSE_QF,
             "unsupported recording mode")
    expected_codec = (CODEC_POSE_ATTACHMENTS
                      if version >= GHOST_VERSION_V4 else CODEC_RAW)
    _require(sample_codec == expected_codec, "unsupported sample codec")
    _require(sample_stride == SAMPLE_SIZE, "invalid sample stride")
    _require(sample_interval_qf == SAMPLE_INTERVAL_QF, "invalid sample interval")
    _require(not (run_flags & RUN_TAS) or (run_flags & RUN_ASSISTED),
             "TAS ghost must be marked assisted")
    _require(source_profile < PROFILE_COUNT, "invalid source profile")
    region_name = _game_region(game_id, region, disc_revision)
    _validate_route(route_area, route_episode, route_parent_area, route_flags,
                    route_variant, "route")

    _require(MIN_SAMPLE_COUNT <= sample_count <= (V6_MAX_SAMPLE_COUNT if version == 6 else MAX_SAMPLE_COUNT),
             "sample count outside canonical bounds")
    _require(0 < duration_qf <= (V6_MAX_DURATION_QF if version == 6 else MAX_DURATION_QF),
             "ghost duration outside its version limit")
    _require(start_qf <= QF_MAX and end_qf <= QF_MAX,
             "ghost QFT boundary exceeds signed runtime range")
    _require(end_qf >= start_qf, "ghost QFT boundaries regress")
    _require(duration_qf == end_qf - start_qf,
             "duration does not match QFT boundaries")
    author = _text(data, 96, AUTHOR_SIZE, author_length, "author")
    name = _text(data, 120, NAME_SIZE, name_length, "name", required=True)
    profile_name = _text(
        data, 168, PROFILE_NAME_SIZE, profile_name_length, "profile name"
    )
    if result_qf != RESULT_QF_NONE:
        _require(start_qf <= result_qf <= end_qf,
                 "QFT result is outside recording boundaries")

    payload = data[header_size:file_size]
    _require(payload_checksum == _crc32(payload), "ghost payload checksum mismatch")
    segments: list[dict] = []

    attachment_descriptors: list[dict] = []
    attachment_flags = 0
    if version == GHOST_VERSION_V3:
        (
            segment_count, segment_size, segment_table_offset,
            segment_table_size, sample_data_offset, sample_data_size,
            segment_table_checksum, *extension_reserved,
        ) = _V3_EXTENSION.unpack_from(data, 184)
        _require(not any(extension_reserved),
                 "nonzero V3 extension reserved bytes")
    else:
        extension = _V4_EXTENSION.unpack_from(data, 184)
        (
            segment_count, segment_size, segment_table_offset,
            segment_table_size, sample_data_offset, sample_data_size,
            segment_table_checksum, attachment_count, attachment_size,
            attachment_flags, attachment_reserved, *descriptor_values,
        ) = extension
        _require(attachment_count <= V4_ATTACHMENT_DESCRIPTOR_COUNT,
                 "V4 attachment descriptor count outside bounds")
        _require(attachment_size == V4_ATTACHMENT_DESCRIPTOR_SIZE,
                 "invalid V4 attachment descriptor size")
        _require(not attachment_flags & ~V4_ATTACHMENT_FLAGS,
                 "unknown V4 attachment flags")
        _require(attachment_reserved == 0,
                 "nonzero V4 attachment reserved field")
        descriptors = list(zip(descriptor_values[::2], descriptor_values[1::2]))
        for index, (object_id, name_key) in enumerate(descriptors):
            if index < attachment_count:
                _require(object_id != 0 or name_key != 0,
                         f"V4 attachment descriptor {index} is empty")
                _require((object_id, name_key) not in descriptors[:index],
                         f"V4 attachment descriptor {index} is duplicated")
                attachment_descriptors.append({
                    "object_id": f"{object_id:08x}",
                    "name_key": f"{name_key:04x}",
                })
            else:
                _require(object_id == 0 and name_key == 0,
                         "nonzero unused V4 attachment descriptor")

    label = f"V{version}"
    _require(1 <= segment_count <= V4_MAX_SEGMENTS,
             f"{label} segment count outside bounds")
    _require(segment_size == V4_SEGMENT_SIZE,
             f"invalid {label} segment descriptor size")
    _require(segment_table_offset == V4_SEGMENT_TABLE_OFFSET and
             segment_table_size == V4_SEGMENT_TABLE_SIZE and
             sample_data_offset == V4_SAMPLE_DATA_OFFSET,
             f"invalid {label} payload offsets")
    _require(sample_data_size == sample_count * sample_stride,
             f"{label} sample-data size does not match sample count")
    teaching = {"input_count": 0, "splits": [], "input_truncated": False}
    _require(payload_size == file_size - header_size,
             f"{label} payload size disagrees with file")
    pose_end = sample_data_offset + sample_data_size
    if version >= GHOST_VERSION_V5:
        teaching = _validate_teaching(data[pose_end:], start_qf, end_qf, version)
    else:
        _require(file_size == pose_end,
                 f"{label} ghost contains trailing or missing bytes")

    table = data[segment_table_offset:sample_data_offset]
    _require(len(table) == V4_SEGMENT_TABLE_SIZE,
             f"truncated {label} segment table")
    _require(segment_table_checksum == _crc32(table),
             f"{label} segment table checksum mismatch")
    _require(not any(table[segment_count * segment_size:]),
             f"nonzero unused {label} segment descriptors")
    sample_data = data[sample_data_offset:pose_end]

    expected_sample = 0
    previous_end = None
    for segment_index in range(segment_count):
        offset = segment_index * segment_size
        (
            first_sample, segment_samples, segment_start, segment_end,
            segment_variant, segment_area, segment_episode, segment_parent,
            segment_flags, reserved0, reserved1,
        ) = _SEGMENT.unpack_from(table, offset)
        _require(first_sample == expected_sample,
                 f"segment {segment_index} sample range is not contiguous")
        _require(segment_samples >= 1 and
                 expected_sample + segment_samples <= sample_count,
                 f"segment {segment_index} sample count is invalid")
        _require(segment_start <= QF_MAX and segment_end <= QF_MAX and
                 segment_end >= segment_start,
                 f"segment {segment_index} QFT range is invalid")
        if previous_end is not None:
            _require(segment_start >= previous_end,
                     f"segment {segment_index} overlaps prior QFT range")
        _validate_route(
            segment_area, segment_episode, segment_parent, segment_flags,
            segment_variant, f"segment {segment_index} route",
        )
        _require(reserved0 == 0 and reserved1 == 0,
                 f"segment {segment_index} reserved fields are nonzero")
        segment_route = {
            "area": segment_area, "episode": segment_episode,
            "parent_area": segment_parent, "flags": segment_flags,
            "variant": segment_variant,
        }
        segments.append({
            "first_sample": first_sample, "sample_count": segment_samples,
            "start_qf": segment_start, "end_qf": segment_end,
            "route": segment_route,
        })
        expected_sample += segment_samples
        previous_end = segment_end

    _require(expected_sample == sample_count,
             f"{label} segments do not cover every sample")
    _require(segments[0]["start_qf"] == start_qf and
             segments[-1]["end_qf"] == end_qf,
             f"{label} segments do not span header QFT boundaries")
    _require(segments[0]["route"] == {
        "area": route_area, "episode": route_episode,
        "parent_area": route_parent_area, "flags": route_flags,
        "variant": route_variant,
    }, f"{label} first segment route differs from header")
    if result_qf != RESULT_QF_NONE:
        _require(segments[-1]["start_qf"] <= result_qf <=
                 segments[-1]["end_qf"],
                 f"QFT result is outside final {label} segment")

    for segment_index, segment in enumerate(segments):
        elapsed = 0
        first = segment["first_sample"]
        count = segment["sample_count"]
        for local_index in range(count):
            index = first + local_index
            if version == GHOST_VERSION_V3:
                (x, y, z, _yaw, delta_qf, animation_id,
                 _phase, reserved) = _unpack_pose_sample(
                    sample_data, index * SAMPLE_SIZE)
                _require(reserved == 0,
                         f"sample {index} animation reserved bits are nonzero")
            else:
                (x, y, z, _yaw, delta_qf, animation_id,
                 _phase, yoshi, held) = _unpack_pose_sample_v4(
                    sample_data, index * SAMPLE_SIZE)
                _require(yoshi <= V4_YOSHI_UNKNOWN,
                         f"sample {index} Yoshi state is invalid")
                _require(held == 0 or held <= len(attachment_descriptors) or
                         (held == V4_HELD_UNKNOWN and
                          attachment_flags & V4_ATTACHMENT_HELD_OVERFLOW),
                         f"sample {index} held descriptor index is invalid")
            _require(abs(x) <= MAX_POSITION_FIXED and
                     abs(y) <= MAX_POSITION_FIXED and
                     abs(z) <= MAX_POSITION_FIXED,
                     f"sample {index} position outside canonical bounds")
            _require(animation_id <= ANIMATION_ID_MAX,
                     f"sample {index} animation id outside bounds")
            if local_index == 0:
                _require(delta_qf == 0,
                         f"segment {segment_index} first delta is nonzero")
            else:
                short_terminal = (local_index + 1 == count and
                                  0 < delta_qf < sample_interval_qf)
                _require(delta_qf >= sample_interval_qf or short_terminal,
                         f"sample {index} has an invalid short delta")
                elapsed += delta_qf
            _require(elapsed <= segment["end_qf"] - segment["start_qf"],
                     f"segment {segment_index} sample timeline overruns")
        _require(elapsed == segment["end_qf"] - segment["start_qf"],
                 f"segment {segment_index} duration differs from samples")

    ghost_id = (ghost_id_hi << 32) | ghost_id_lo
    _require(ghost_id != 0, "zero ghost id")
    created_unix = (created_hi << 32) | created_lo
    return {
        "kind": "ghost",
        "teaching": teaching,
        "version": version,
        "file_size": file_size,
        "file_checksum": file_checksum,
        "payload_checksum": payload_checksum,
        "required_features": required_features,
        "run_flags": run_flags,
        "game_id": f"{game_id:08x}",
        "region": region_name,
        "source_profile": source_profile,
        "route": {
            "area": route_area,
            "episode": route_episode,
            "parent_area": route_parent_area,
            "flags": route_flags,
            "variant": route_variant,
        },
        "result_qf": None if result_qf == RESULT_QF_NONE else result_qf,
        "start_qf": start_qf,
        "end_qf": end_qf,
        "duration_qf": duration_qf,
        "sample_count": sample_count,
        "segment_count": len(segments),
        "segments": segments,
        "attachment_flags": attachment_flags,
        "attachment_descriptors": attachment_descriptors,
        "created_unix": created_unix,
        "ghost_id": f"{ghost_id:016x}",
        "portable_filename": portable_ghost_filename(ghost_id),
        "author": author,
        "name": name,
        "profile_name": profile_name,
    }


def validate_imported_ghost(data: bytes, *, running_region: int) -> dict:
    _require(running_region in REGION_GAME_IDS, "invalid running region")
    ghost = validate_ghost(data)
    source_region = next(
        region for region, name in REGION_NAMES.items()
        if name == ghost["region"]
    )
    if source_region != running_region:
        for index, segment in enumerate(ghost["segments"]):
            _validate_portable_route(segment["route"],
                                     f"segment {index} route")
    return ghost


def validate_index(data: bytes) -> dict:
    _version(data, INDEX_MAGIC, "index")
    _require(len(data) >= INDEX_HEADER_SIZE, "truncated index header")
    fields = _INDEX_PREFIX.unpack_from(data)
    (
        _magic,
        version,
        header_size,
        entry_size,
        entry_count,
        file_size,
        generation,
        file_checksum,
        entries_checksum,
        game_id,
        region,
        profile,
        max_entries,
        flags,
        total_duration_qf,
        quota_duration_qf,
        created_hi,
        created_lo,
        profile_name_length,
        checksum_kind,
        reserved0_a,
        reserved0_b,
    ) = fields

    _require(version == VERSION, "index version changed while parsing")
    _require(header_size == INDEX_HEADER_SIZE, "invalid index header size")
    _require(entry_size == INDEX_ENTRY_SIZE, "invalid index entry size")
    _require(entry_count <= PROFILE_MAX_ENTRIES, "index exceeds 48 ghosts")
    _require(max_entries == PROFILE_MAX_ENTRIES, "index policy count changed")
    _require(profile < PROFILE_COUNT, "invalid index profile")
    _require(quota_duration_qf == PROFILE_MAX_DURATION_QF,
             "index policy duration changed")
    _require(file_size == len(data), "index file size does not match bytes")
    _require(file_size == header_size + entry_count * entry_size,
             "index contains trailing or missing entries")
    _require(file_size <= MAX_INDEX_FILE_SIZE, "index exceeds canonical limit")
    if checksum_kind != CHECKSUM_CRC32:
        raise UnsupportedFeature(
            f"unsupported index checksum kind {checksum_kind}"
        )
    _require(reserved0_a == 0 and reserved0_b == 0, "nonzero index reserved bytes")
    _require(flags == 0, "unknown portable-index flags")
    region_name = _game_region(game_id, region)

    expected_file_crc = _crc32_zeroed(data, (20, 24))
    _require(file_checksum == expected_file_crc, "index file checksum mismatch")
    entries_raw = data[header_size:file_size]
    _require(entries_checksum == _crc32(entries_raw), "index entries checksum mismatch")
    profile_name = _text(
        data, 56, PROFILE_NAME_SIZE, profile_name_length, "index profile name"
    )
    _require(not any(data[72:header_size]), "nonzero reserved index header bytes")

    entries = []
    seen_ids = set()
    summed_duration = 0
    for index in range(entry_count):
        offset = header_size + index * entry_size
        entry = _INDEX_ENTRY_PREFIX.unpack_from(data, offset)
        (
            ghost_id_hi,
            ghost_id_lo,
            ghost_file_size,
            ghost_file_checksum,
            duration_qf,
            result_qf,
            start_qf,
            end_qf,
            sample_count,
            ghost_created_hi,
            ghost_created_lo,
            route_variant,
            route_area,
            route_episode,
            route_parent_area,
            route_flags,
            name_length,
            author_length,
            entry_flags,
        ) = entry
        ghost_id = (ghost_id_hi << 32) | ghost_id_lo
        _require(ghost_id != 0, f"entry {index} has zero ghost id")
        _require(ghost_id not in seen_ids, f"entry {index} duplicates ghost id")
        seen_ids.add(ghost_id)
        _validate_route(route_area, route_episode, route_parent_area, route_flags,
                        route_variant, f"entry {index} route")
        _require((entry_flags & ~0x0003) == 0,
                 f"entry {index} has unknown flags")
        _require(MIN_SAMPLE_COUNT <= sample_count <= (V6_MAX_SAMPLE_COUNT if version == 6 else MAX_SAMPLE_COUNT),
                 f"entry {index} sample count outside bounds")
        _require(duration_qf <= MAX_DURATION_QF,
                 f"entry {index} duration exceeds 15 minutes")
        _require(duration_qf != 0, f"entry {index} duration is zero")
        _require(start_qf <= QF_MAX and end_qf <= QF_MAX,
                 f"entry {index} QFT boundary exceeds runtime range")
        _require(end_qf >= start_qf and duration_qf == end_qf - start_qf,
                 f"entry {index} duration differs from boundaries")
        _require(ghost_file_size ==
                     V4_SAMPLE_DATA_OFFSET + sample_count * SAMPLE_SIZE,
                 f"entry {index} file size does not match canonical samples")
        _require(ghost_file_size <= MAX_GHOST_FILE_SIZE,
                 f"entry {index} file is too large")
        if result_qf != RESULT_QF_NONE:
            _require(start_qf <= result_qf <= end_qf,
                     f"entry {index} result is outside boundaries")
        name = _text(data, offset + 56, NAME_SIZE, name_length,
                     f"entry {index} name", required=True)
        author = _text(data, offset + 104, AUTHOR_SIZE, author_length,
                       f"entry {index} author")
        summed_duration += duration_qf
        entries.append({
            "ghost_id": f"{ghost_id:016x}",
            "portable_filename": portable_ghost_filename(ghost_id),
            "file_size": ghost_file_size,
            "file_checksum": ghost_file_checksum,
            "duration_qf": duration_qf,
            "result_qf": None if result_qf == RESULT_QF_NONE else result_qf,
            "start_qf": start_qf,
            "end_qf": end_qf,
            "sample_count": sample_count,
            "created_unix": (ghost_created_hi << 32) | ghost_created_lo,
            "route": {
                "area": route_area,
                "episode": route_episode,
                "parent_area": route_parent_area,
                "flags": route_flags,
                "variant": route_variant,
            },
            "game_id": f"{game_id:08x}",
            "flags": entry_flags,
            "name": name,
            "author": author,
        })

    _require(summed_duration == total_duration_qf,
             "index total duration does not match entries")
    _require(summed_duration <= PROFILE_MAX_DURATION_QF,
             "index exceeds 10-hour profile quota")
    return {
        "kind": "index",
        "version": version,
        "file_size": file_size,
        "file_checksum": file_checksum,
        "generation": generation,
        "game_id": f"{game_id:08x}",
        "region": region_name,
        "profile": profile,
        "profile_name": profile_name,
        "flags": flags,
        "total_duration_qf": total_duration_qf,
        "created_unix": (created_hi << 32) | created_lo,
        "entry_count": entry_count,
        "entries": entries,
    }


def generation_is_newer(candidate: int, current: int) -> bool:
    candidate &= 0xFFFFFFFF
    current &= 0xFFFFFFFF
    delta = (candidate - current) & 0xFFFFFFFF
    return delta != 0 and delta < 0x80000000


def select_newest_index(candidates: list[tuple[str, bytes]]) -> tuple[str, dict]:
    # An old reader must not select and later overwrite a stale generation when
    # either journal is a recognizable forward-version index.
    for _name, data in candidates:
        if len(data) >= 6 and data[:4] == INDEX_MAGIC:
            version = struct.unpack_from(">H", data, 4)[0]
            if version > VERSION:
                raise UnsupportedVersion(
                    f"index version {version} is newer than reader {VERSION}"
                )

    valid = []
    errors = []
    for name, data in candidates:
        try:
            valid.append((name, validate_index(data), data))
        except UnsupportedVersion:
            raise
        except FormatError as error:
            errors.append(f"{name}: {error}")
    if not valid:
        raise FormatError("no valid index generation; " + "; ".join(errors))
    selected = valid[0]
    for candidate in valid[1:]:
        a = candidate[1]["generation"]
        b = selected[1]["generation"]
        delta = (a - b) & 0xFFFFFFFF
        if delta == 0x80000000:
            raise FormatError("ambiguous index generations")
        if generation_is_newer(a, b):
            selected = candidate
        elif a == b and candidate[2] != selected[2]:
            raise FormatError("equal index generations contain different data")
    return selected[0], selected[1]


def validate_bundle(index_data: bytes, directory: Path) -> dict:
    """Validate the future portable SGIX bundle, not console A/B slots."""
    summary = validate_index(index_data)
    for number, entry in enumerate(summary["entries"]):
        path = directory / entry["portable_filename"]
        raw = _read_bounded(path, GHOST_MAGIC)
        ghost = validate_ghost(raw)
        _require(ghost["ghost_id"] == entry["ghost_id"],
                 f"entry {number} ghost id differs from file")
        _require(ghost["file_size"] == entry["file_size"],
                 f"entry {number} cached file size differs")
        _require(ghost["file_checksum"] == entry["file_checksum"],
                 f"entry {number} cached checksum differs")
        _require(ghost["duration_qf"] == entry["duration_qf"],
                 f"entry {number} cached duration differs")
        _require(ghost["start_qf"] == entry["start_qf"] and
                 ghost["end_qf"] == entry["end_qf"],
                 f"entry {number} cached QFT boundaries differ")
        _require(ghost["result_qf"] == entry["result_qf"],
                 f"entry {number} cached result differs")
        _require(ghost["sample_count"] == entry["sample_count"],
                 f"entry {number} cached sample count differs")
        _require(ghost["game_id"] == summary["game_id"],
                 f"entry {number} file belongs to another region")
        _require(ghost["source_profile"] == summary["profile"],
                 f"entry {number} file belongs to another profile")
        _require(ghost["profile_name"] == summary["profile_name"],
                 f"entry {number} cached profile name differs")
        _require(ghost["route"] == entry["route"],
                 f"entry {number} cached route differs")
        _require(ghost["name"] == entry["name"] and
                 ghost["author"] == entry["author"],
                 f"entry {number} cached display text differs")
    summary["bundle_directory"] = str(directory)
    return summary


def _validate_one(path: Path, bundle_dir: Path | None) -> dict:
    data = _read_bounded(path)
    if data[:4] == GHOST_MAGIC:
        return validate_ghost(data)
    if data[:4] == INDEX_MAGIC:
        return validate_bundle(data, bundle_dir) if bundle_dir else validate_index(data)
    raise FormatError("unknown file magic")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("--bundle-dir", type=Path,
                        help="also validate host-export files referenced by SGIX")
    parser.add_argument("--select-index", action="store_true",
                        help="select the newest valid A/B index generation")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    try:
        if args.select_index:
            if len(args.files) != 2:
                parser.error("--select-index requires exactly two files")
            candidates = [
                (str(path), _read_bounded(path, INDEX_MAGIC))
                for path in args.files
            ]
            selected, summary = select_newest_index(candidates)
            result = {"selected": selected, "index": summary}
        else:
            result = {
                str(path): _validate_one(path, args.bundle_dir)
                for path in args.files
            }
    except (UnsupportedVersion, UnsupportedFeature) as error:
        print(f"unsupported: {error}", file=sys.stderr)
        return 3
    except (FormatError, OSError) as error:
        print(f"invalid: {error}", file=sys.stderr)
        return 2

    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        if args.select_index:
            print(f"valid: selected {result['selected']}")
        else:
            for name, summary in result.items():
                detail = (f"{summary['sample_count']} samples"
                          if summary["kind"] == "ghost"
                          else f"{summary['entry_count']} entries, generation "
                               f"{summary['generation']}")
                print(f"valid: {name}: {summary['kind']} V{summary['version']} ({detail})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
