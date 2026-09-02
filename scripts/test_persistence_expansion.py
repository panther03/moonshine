#!/usr/bin/env python3
"""Host checks for the 136-slot PB/profile persistence migration."""

from __future__ import annotations

from pathlib import Path
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "include" / "susamune" / "susamune_cfg.h"
MEM2 = ROOT / "include" / "susamune" / "mem2_map.h"
KERNEL = ROOT / "launcher" / "kernel" / "SusamuneCfg.c"
EMULATOR = ROOT / "src" / "emulator_persistence.cpp"

PROFILE_MAGIC = 0x53505246
PROFILE_CFG_MAGIC = 0x53495052
PROFILE_VERSION_V1 = 1
PROFILE_VERSION = 2
PROFILE_COUNT = 4
OLD_SLOTS = 128
SLOTS = 136
NAME_SIZE = 16
CUSTOM_NAMES = 2
UNSET = -1
MAX_QF = 0x000AF9B0
GAME_ID = 0x474D534A

PROFILE_FILE_HEADER = struct.Struct(">IHBBHHIII8s")
PROFILE_CFG_HEADER = struct.Struct(">IHBBHHI16sII24s")
PROFILE_FILE_V1_SIZE = 2112
PROFILE_FILE_SIZE = 2240
PROFILE_CFG_V1_SIZE = 2144
PROFILE_CFG_SIZE = 2272

CARD_MAGIC = 0x53554346
CFG_MAGIC = 0x53434647
CFG_VERSION = 2
CARD_VERSION_V5 = 5
CARD_VERSION = 6
CARD_SECTOR_SIZE = 0x2000
CFG_V5_SIZE = 5016
CFG_SIZE = 5144
PROFILE_CFG_OFFSET = 2784
OLD_MOVEMENT_OFFSET = 4928
MOVEMENT_OFFSET = 5056
MOVEMENT_SIZE = 88
CARD_HEADER = struct.Struct(">IHHIII12s")


def hash_word(value: int, word: int) -> int:
    return ((value ^ (word & 0xFFFFFFFF)) * 16777619) & 0xFFFFFFFF


def hash_byte(value: int, byte: int) -> int:
    return ((value ^ byte) * 16777619) & 0xFFFFFFFF


def profile_checksum(
    version: int,
    active: int,
    slot_count: int,
    generation: int,
    values: tuple[tuple[int, ...], ...],
    names: bytes,
) -> int:
    value = 2166136261
    value = hash_word(
        value, (version << 16) | (PROFILE_COUNT << 8) | active
    )
    value = hash_word(value, (slot_count << 16) | NAME_SIZE)
    value = hash_word(value, GAME_ID)
    value = hash_word(value, generation)
    for profile in values:
        for pb in profile:
            value = hash_word(value, pb)
    for byte in names:
        value = hash_byte(value, byte)
    return value


def fixed_name(value: bytes) -> bytes:
    if len(value) >= NAME_SIZE:
        raise ValueError("name must include a terminator")
    return value + bytes(NAME_SIZE - len(value))


def build_profile_v1(
    values: tuple[tuple[int, ...], ...], names: bytes,
    *, active: int = 2, generation: int = 41,
) -> bytes:
    if len(values) != PROFILE_COUNT or any(
        len(profile) != OLD_SLOTS for profile in values
    ):
        raise ValueError("wrong V1 profile dimensions")
    if len(names) != CUSTOM_NAMES * NAME_SIZE:
        raise ValueError("wrong name payload")
    digest = profile_checksum(
        PROFILE_VERSION_V1, active, OLD_SLOTS, generation, values, names
    )
    header = PROFILE_FILE_HEADER.pack(
        PROFILE_MAGIC,
        PROFILE_VERSION_V1,
        PROFILE_COUNT,
        active,
        OLD_SLOTS,
        NAME_SIZE,
        GAME_ID,
        generation,
        digest,
        bytes(8),
    )
    payload = b"".join(
        struct.pack(">" + "i" * OLD_SLOTS, *profile) for profile in values
    )
    raw = header + payload + names
    if len(raw) != PROFILE_FILE_V1_SIZE:
        raise AssertionError("V1 profile file layout changed")
    return raw


def migrate_profile_v1(raw: bytes) -> tuple[int, tuple[tuple[int, ...], ...], bytes]:
    if len(raw) != PROFILE_FILE_V1_SIZE:
        raise ValueError("wrong V1 profile file size")
    (
        magic, version, profile_count, active, slot_count, name_size,
        game_id, generation, digest, reserved,
    ) = PROFILE_FILE_HEADER.unpack_from(raw)
    offset = PROFILE_FILE_HEADER.size
    values = tuple(
        struct.unpack_from(">" + "i" * OLD_SLOTS, raw, offset + p * OLD_SLOTS * 4)
        for p in range(PROFILE_COUNT)
    )
    names = raw[offset + PROFILE_COUNT * OLD_SLOTS * 4:]
    if version != PROFILE_VERSION_V1:
        raise RuntimeError("future profile version must be preserved")
    if (
        magic != PROFILE_MAGIC
        or profile_count != PROFILE_COUNT
        or active >= PROFILE_COUNT
        or slot_count != OLD_SLOTS
        or name_size != NAME_SIZE
        or game_id != GAME_ID
        or reserved != bytes(8)
        or digest != profile_checksum(
            version, active, slot_count, generation, values, names
        )
        or any(pb < UNSET or pb > MAX_QF for profile in values for pb in profile)
    ):
        raise ValueError("invalid V1 profile file")
    migrated = tuple(profile + (UNSET,) * (SLOTS - OLD_SLOTS) for profile in values)
    return active, migrated, names


def card_checksum(raw: bytes) -> int:
    mutable = bytearray(raw)
    mutable[12:16] = bytes(4)
    value = 2166136261
    for byte in mutable:
        value = hash_byte(value, byte)
    return value


def build_v5_card(
    values: tuple[tuple[int, ...], ...], names: bytes, movement: bytes,
) -> bytes:
    cfg = bytearray(CFG_V5_SIZE)
    struct.pack_into(">IHH", cfg, 0, CFG_MAGIC, CFG_VERSION, 0)
    profile_header = PROFILE_CFG_HEADER.pack(
        PROFILE_CFG_MAGIC,
        PROFILE_VERSION_V1,
        PROFILE_COUNT,
        3,
        OLD_SLOTS,
        NAME_SIZE,
        7,
        bytes(16),
        0,
        0,
        bytes(24),
    )
    profile_values = b"".join(
        struct.pack(">" + "i" * OLD_SLOTS, *profile) for profile in values
    )
    old_profiles = profile_header + profile_values + names
    if len(old_profiles) != PROFILE_CFG_V1_SIZE:
        raise AssertionError("V1 profile cfg layout changed")
    cfg[PROFILE_CFG_OFFSET:OLD_MOVEMENT_OFFSET] = old_profiles
    cfg[OLD_MOVEMENT_OFFSET:OLD_MOVEMENT_OFFSET + MOVEMENT_SIZE] = movement

    raw = bytearray(CARD_SECTOR_SIZE)
    CARD_HEADER.pack_into(
        raw, 0, CARD_MAGIC, CARD_VERSION_V5, CFG_V5_SIZE, 29, 0, 1, bytes(12)
    )
    raw[CARD_HEADER.size:CARD_HEADER.size + CFG_V5_SIZE] = cfg
    struct.pack_into(">I", raw, 12, card_checksum(raw))
    return bytes(raw)


def migrate_v5_card(raw: bytes) -> bytearray:
    magic, version, payload_size, _, digest, _, _ = CARD_HEADER.unpack_from(raw)
    if (
        len(raw) != CARD_SECTOR_SIZE
        or magic != CARD_MAGIC
        or version != CARD_VERSION_V5
        or payload_size != CFG_V5_SIZE
        or digest != card_checksum(raw)
    ):
        raise ValueError("invalid V5 card record")
    old = raw[CARD_HEADER.size:CARD_HEADER.size + CFG_V5_SIZE]
    new = bytearray(CFG_SIZE)
    new[:PROFILE_CFG_OFFSET] = old[:PROFILE_CFG_OFFSET]
    old_profile = old[PROFILE_CFG_OFFSET:OLD_MOVEMENT_OFFSET]
    header = PROFILE_CFG_HEADER.unpack_from(old_profile)
    active = header[3]
    slot_count = header[4]
    offset = PROFILE_CFG_HEADER.size
    values = tuple(
        struct.unpack_from(">" + "i" * OLD_SLOTS, old_profile,
                           offset + p * OLD_SLOTS * 4)
        for p in range(PROFILE_COUNT)
    )
    names = old_profile[offset + PROFILE_COUNT * OLD_SLOTS * 4:]
    current_header = PROFILE_CFG_HEADER.pack(
        PROFILE_CFG_MAGIC,
        PROFILE_VERSION,
        PROFILE_COUNT,
        active,
        SLOTS,
        NAME_SIZE,
        header[6],
        header[7],
        header[8],
        header[9],
        header[10],
    )
    migrated_values = b"".join(
        struct.pack(">" + "i" * SLOTS,
                    *(profile[:slot_count] + (UNSET,) * (SLOTS - slot_count)))
        for profile in values
    )
    current_profiles = current_header + migrated_values + names
    if len(current_profiles) != PROFILE_CFG_SIZE:
        raise AssertionError("current profile cfg layout changed")
    new[PROFILE_CFG_OFFSET:MOVEMENT_OFFSET] = current_profiles
    new[MOVEMENT_OFFSET:MOVEMENT_OFFSET + MOVEMENT_SIZE] = old[
        OLD_MOVEMENT_OFFSET:OLD_MOVEMENT_OFFSET + MOVEMENT_SIZE
    ]
    return new


class ProfileFileMigrationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.values = tuple(
            tuple(UNSET if slot % 11 == 0 else profile * 10000 + slot
                  for slot in range(OLD_SLOTS))
            for profile in range(PROFILE_COUNT)
        )
        self.names = fixed_name(b"Practice") + fixed_name(b"Races")

    def test_v1_values_names_and_active_bank_survive(self) -> None:
        active, values, names = migrate_profile_v1(
            build_profile_v1(self.values, self.names)
        )
        self.assertEqual(active, 2)
        self.assertEqual(names, self.names)
        for profile in range(PROFILE_COUNT):
            self.assertEqual(values[profile][:OLD_SLOTS], self.values[profile])
            self.assertEqual(values[profile][OLD_SLOTS:], (UNSET,) * 8)

    def test_v5_card_moves_names_and_movement_instead_of_raw_copying(self) -> None:
        movement = bytes((index * 7 + 3) & 0xFF for index in range(MOVEMENT_SIZE))
        cfg = migrate_v5_card(build_v5_card(self.values, self.names, movement))
        names_offset = PROFILE_CFG_OFFSET + 64 + PROFILE_COUNT * SLOTS * 4
        self.assertEqual(names_offset, 5024)
        self.assertEqual(cfg[names_offset:names_offset + len(self.names)], self.names)
        self.assertEqual(cfg[MOVEMENT_OFFSET:MOVEMENT_OFFSET + MOVEMENT_SIZE], movement)
        for profile in range(PROFILE_COUNT):
            tail = PROFILE_CFG_OFFSET + 64 + profile * SLOTS * 4 + OLD_SLOTS * 4
            self.assertEqual(cfg[tail:tail + 8 * 4], b"\xff" * (8 * 4))


class PersistenceSourceContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.mem2 = MEM2.read_text(encoding="utf-8")
        cls.kernel = KERNEL.read_text(encoding="utf-8")
        cls.emulator = EMULATOR.read_text(encoding="utf-8")

    def test_legacy_and_current_profile_abis_are_both_explicit(self) -> None:
        for contract in (
            "SUSAMUNE_ILING_PB_LEGACY_MAX_SLOTS    128u",
            "SUSAMUNE_ILING_PB_SLOT_COUNT          136u",
            "SUSAMUNE_ILING_PROFILE_VERSION_V1 1u",
            "SUSAMUNE_ILING_PROFILE_VERSION    2u",
            "sizeof(struct SusamuneILingProfilesFileV1) == 2112",
            "sizeof(struct SusamuneILingProfilesFile) == 2240",
            "sizeof(struct SusamuneCfg) == 5144",
        ):
            self.assertIn(contract, self.header)

    def test_console_reader_rewrites_migrated_v1_generation(self) -> None:
        self.assertIn("PbProfilesChecksumV1", self.kernel)
        self.assertIn("sizeof(struct SusamuneILingProfilesFileV1)", self.kernel)
        self.assertIn("file->values[p][i] = v1->values[p][i];", self.kernel)
        self.assertIn("selectedMigrated = migrated;", self.kernel)
        self.assertIn("profiles->saveSeq = 1;", self.kernel)

    def test_live_mirrors_exactly_fit_four_current_profiles(self) -> None:
        self.assertEqual(PROFILE_COUNT * SLOTS * 4, 0x880)
        for contract in (
            "SUSAMUNE_DOLPHIN_PB_LIVE_PPC_BASE    0x70FFF780u",
            "SUSAMUNE_DOLPHIN_PB_LIVE_SIZE        0x00000880u",
            "SUSAMUNE_MEM2_PB_LIVE_PPC_BASE       0x92EFF780u",
            "SUSAMUNE_MEM2_PB_LIVE_SIZE           0x00000880u",
        ):
            self.assertIn(contract, self.mem2)

    def test_dolphin_v5_migration_has_explicit_shifted_fields(self) -> None:
        for contract in (
            "constexpr u16 kRecordVersion = 6;",
            "struct RecordV5",
            "u8 cfg[5016];",
            "kProfilesOffsetV1 = 2784",
            "kMovementOffsetV5 == 4928",
            "migrateProfilesV1(cfg, oldCfg)",
            "oldCfg + kMovementOffsetV5",
        ):
            self.assertIn(contract, self.emulator)


if __name__ == "__main__":
    unittest.main()
