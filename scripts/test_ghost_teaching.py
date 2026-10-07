"""V5 teaching fixtures exercise the independent host and production C decoders."""

from pathlib import Path
import ctypes
import struct
import subprocess
import tempfile
import unittest
import zlib

import validate_ghost as ghost
import validate_ghost_storage as storage
from test_ghost_format import build_ghost
from test_ghost_storage import envelope, rechecksum_ghost

ROOT = Path(__file__).resolve().parents[1]
INPUT = struct.Struct(">IHbbbbBBBBbB")
SPLIT = struct.Struct(">IIHBB")


def teaching_file(inputs=None, splits=None, base=None, flags=0, fludd=None):
    if base is None:
        base = build_ghost(version=4)
    if inputs is None:
        inputs = [(100, 0x100, -40, 30, 0, 0, 70, 0, 0, 0, 0, 0),
                  (102, 0, 20, -30, 30, 0, 0, 40, 0, 0, 0, 0),
                  (104, 0x200, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)]
    if splits is None:
        splits = [(102, 0x1AF7E430, 0, 0, 0),
                  (104, 0x1AF7E430, 0, 1, 0)]
    if fludd is not None:
        assert len(fludd) == len(inputs)
    data = b"".join(INPUT.pack(*item) + (fludd[i] if fludd is not None else b"")
                    for i, item in enumerate(inputs))
    data += b"".join(SPLIT.pack(*item) for item in splits)
    section = struct.pack(">IHH6I", 0x53475449, 2 if fludd is not None else 1, 32, len(inputs),
                          len(splits), flags, zlib.crc32(data), 0, 0) + data
    out = bytearray(base + section)
    struct.pack_into(">H", out, 4, 6 if fludd is not None else 5)
    struct.pack_into(">I", out, 8, len(out))
    struct.pack_into(">I", out, 24, 7 if fludd is not None else 3)
    struct.pack_into(">I", out, 72, len(out) - 256)
    return rechecksum_ghost(bytes(out))


def mutate_section(data, offset, value):
    raw = bytearray(data)
    pose_end = 0x900 + struct.unpack_from(">I", raw, 68)[0] * 16
    raw[pose_end + offset:pose_end + offset + len(value)] = value
    struct.pack_into(">I", raw, pose_end + 20,
                     zlib.crc32(raw[pose_end + 32:]))
    return rechecksum_ghost(raw)


class TeachingTests(unittest.TestCase):
    def test_v6_all_nozzles_aim_and_emission(self):
        for nozzle in range(6):
            for pitch in (0, 1024, 3072, 4095):
                observation = bytes([0x18 | nozzle, 0x81, 127, 30, 255,
                                     0xf0 | (pitch >> 8), pitch & 255, 70])
                result = ghost.validate_ghost(teaching_file(fludd=[observation] * 3))
                self.assertEqual(result['version'], 6)
                self.assertEqual(result['teaching']['input_count'], 3)
        ghost.validate_ghost(teaching_file(fludd=[bytes(8)] * 3))

    def test_v6_invalid_observation_and_version_pair(self):
        data = teaching_file(fludd=[bytes(8)] * 3)
        for sample in (bytes([1,0,0,0,0,0,0,0]), bytes([0,1,0,0,0,0,0,0]),
                       bytes([14,0,0,0,0,0,0,0]), bytes([8,0,0,0,0,0,0,1]),
                       bytes([8,0,0,0,0,4,1,0]), bytes([0x38,0,0,0,0,0,0,0])):
            with self.subTest(sample=sample), self.assertRaises(ghost.FormatError):
                ghost.validate_ghost(mutate_section(data, 32 + 16, sample))
        with self.assertRaises(ghost.FormatError):
            ghost.validate_ghost(mutate_section(data, 4, b'\x00\x01'))
        with self.assertRaises(ghost.FormatError):
            ghost.validate_ghost(mutate_section(teaching_file(), 4, b'\x00\x02'))

    def test_v6_maximum_reuses_legacy_input_budget(self):
        samples = [(0,0,0,0,0,0,0)] + [(0,0,0,0,4,0,0)] * 17982
        inputs = [(i*2,0,0,0,0,0,0,0,0,0,0,0) for i in range(35965)]
        base = build_ghost(version=4, samples=samples, start_qf=0)
        data = teaching_file(inputs, [], base, fludd=[bytes(8)] * len(inputs))
        self.assertEqual(ghost.validate_ghost(data)['end_qf'], 71928)
        self.assertEqual(36000 * 24, 54000 * 16)
        self.assertLessEqual(len(data), 1298016)
        # A long legacy file remains valid, but cannot masquerade as V6.
        base = build_ghost(version=4, samples=samples + [(0,0,0,0,4,0,0)], start_qf=0)
        ghost.validate_ghost(teaching_file([], [], base))
        with self.assertRaises(ghost.FormatError):
            ghost.validate_ghost(teaching_file([], [], base, fludd=[]))

    def test_v5_input_split_and_envelope_round_trip(self):
        data = teaching_file()
        result = ghost.validate_ghost(data)
        self.assertEqual(result["version"], 5)
        self.assertEqual(result["teaching"]["input_count"], 3)
        self.assertEqual([s["qf"] for s in result["teaching"]["splits"]], [102, 104])
        storage.validate_slot_file(envelope(data), game_id=0x474D534A, profile=0, slot=0)

    def test_tas_flag_requires_assisted_and_preserves_legacy_flags(self):
        for version in (3, 4):
            base = build_ghost(version=version, run_flags=0x21)
            data = teaching_file(base=base) if version == 4 else base
            self.assertEqual(ghost.validate_ghost(data)["run_flags"], 0x21)
        with self.assertRaisesRegex(ghost.FormatError, "TAS ghost must"):
            ghost.validate_ghost(teaching_file(base=build_ghost(version=4, run_flags=0x20)))
        self.assertEqual(ghost.validate_ghost(build_ghost(version=4, run_flags=0x80000000))[
            "run_flags"], 0x80000000)

    def test_legacy_ghosts_do_not_invent_inputs(self):
        for version in (3, 4):
            result = ghost.validate_ghost(build_ghost(version=version))
            self.assertEqual(result["teaching"]["input_count"], 0)
            self.assertEqual(result["teaching"]["splits"], [])

    def test_empty_optional_streams_and_simultaneous_splits(self):
        result = ghost.validate_ghost(teaching_file(inputs=[], splits=[
            (104, 0x1AF7E430, 0, 0, 0), (104, 0x1AF7E430, 0, 1, 0)]))
        self.assertEqual(result["teaching"]["input_count"], 0)
        ghost.validate_ghost(teaching_file(inputs=[], splits=[]))

    def test_lost_inputs_are_explicit(self):
        result = ghost.validate_ghost(teaching_file(flags=1))
        self.assertTrue(result["teaching"]["input_truncated"])

    def test_bounds_order_flags_and_schema_rejected(self):
        data = teaching_file()
        changes = [
            (8, struct.pack(">I", 54001)),
            (12, struct.pack(">I", 9)),
            (16, struct.pack(">I", 2)),
            (24, b"\x01"),
            (32, struct.pack(">I", 99)),
            (48, struct.pack(">I", 100)),
            (64, struct.pack(">I", 105)),
            (32 + 15, b"\x01"),
            (32 + 4, b"\x80"),
            (32 + 3 * 16 + 12 + 4, struct.pack(">I", 0x12345678)),
            (32 + 3 * 16 + 12 + 10, b"\x00"),
        ]
        for offset, value in changes:
            with self.subTest(offset=offset):
                with self.assertRaises(ghost.FormatError):
                    ghost.validate_ghost(mutate_section(data, offset, value))

    def test_inner_and_outer_checksums_are_independent(self):
        data = bytearray(teaching_file())
        data[-1] ^= 1
        with self.assertRaisesRegex(ghost.FormatError, "file checksum"):
            ghost.validate_ghost(data)
        with self.assertRaisesRegex(ghost.FormatError, "teaching checksum"):
            ghost.validate_ghost(rechecksum_ghost(data))

    def test_maximum_inputs_and_poses_fit_reserved_transfer(self):
        samples = [(0, 0, 0, 0, 0, 0, 0)]
        samples += [(0, 0, 0, 0, 4, 0, 0)] * 26973
        base = build_ghost(version=4, samples=samples, start_qf=0)
        inputs = [(i, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) for i in range(54000)]
        splits = [(i, 0x1AF7E430, 0, i, 0) for i in range(8)]
        data = teaching_file(inputs, splits, base)
        self.assertEqual(len(data), 1298016)
        self.assertLessEqual(len(data), 0x13E000)
        self.assertLessEqual(len(inputs) * 16, 0xE0000)
        self.assertEqual(ghost.validate_ghost(data)["teaching"]["input_count"], 54000)

    def test_production_arm_chunked_validator(self):
        compiler = ROOT / "toolchain/clang.exe"
        if not compiler.exists():
            self.skipTest("bundled Windows compiler unavailable")
        kernel = (ROOT / "launcher/kernel/SusamuneGhost.c").read_text()
        globals_ = kernel[kernel.index("static const u8 *ValidationBytes;"):
                          kernel.index("static volatile struct SusamuneGhostStorageMailbox *GhostBlock")]
        functions = kernel[kernel.index("static u16 ReadBe16("):
                           kernel.index("static bool GenerationIsNewer(")]
        # Keep canonical decoding/validation, excluding envelope serialization and I/O.
        functions = functions[:functions.index("static void WriteBe16(")] + functions[
            functions.index("static bool BytesAreZero("):]
        shim = '''#include "susamune/ghost_storage.h"
 typedef unsigned char u8; typedef unsigned short u16;
 typedef unsigned int u32; typedef int s32; typedef _Bool bool;
 #define true 1
 #define false 0
 #define GAME_ID 0x474D534Au
 enum ValidateResult { VALIDATE_INVALID, VALIDATE_OK, VALIDATE_FORWARD };
 static int memcmp(const void *aa,const void *bb,unsigned long long n) {
  const u8 *a=aa,*b=bb; while(n--) { if(*a!=*b)return *a-*b; ++a;++b; } return 0; }
'''
        wrapper = '''
 __declspec(dllexport) int validate(const u8 *p,u32 n) {
  int r=BeginCanonicalValidation(p,n,0,false); unsigned int passes=0;
  if(r!=VALIDATE_OK)return r;
  do { u32 before=ValidationOffset; r=ContinueCanonicalValidation();
   if(ValidationOffset-before>0x4000 || ++passes>200 || (r<0 && ValidationOffset<=before))return -2;
  } while(r<0);
  return r;
 }
'''
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            source, library = folder / "arm.c", folder / "arm.dll"
            source.write_text(shim + globals_ + functions + wrapper)
            subprocess.run([str(compiler), "--target=x86_64-pc-windows-msvc",
                            "-shared", "-nostdlib", "-fuse-ld=lld", "-Wl,/noentry",
                            "-I", str(ROOT / "include"), str(source), "-o", str(library)],
                           check=True, capture_output=True)
            decoder = ctypes.CDLL(str(library))
            decoder.validate.argtypes = [ctypes.c_void_p, ctypes.c_uint]
            decoder.validate.restype = ctypes.c_int
            try:
                valid = [build_ghost(version=3), build_ghost(version=4), teaching_file(),
                         teaching_file(fludd=[bytes([0x18, 0, 25, 0, 0, 0, 0, 65])] * 3),
                         teaching_file(base=build_ghost(version=4, run_flags=0x21)),
                         teaching_file(inputs=[], splits=[]),
                         teaching_file(inputs=[], splits=[(104, 1, 0, 0, 0)])]
                samples = [(0, 0, 0, 0, 0, 0, 0)] + [(0, 0, 0, 0, 4, 0, 0)] * 26973
                inputs = [(i, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) for i in range(54000)]
                valid.append(teaching_file(inputs, [(i, 1, 0, i, 0) for i in range(8)],
                                           build_ghost(version=4, samples=samples, start_qf=0)))
                for data in valid:
                    self.assertEqual(decoder.validate(data, len(data)), 1)
                bad = [teaching_file(base=build_ghost(version=4, run_flags=0x20)),
                       teaching_file(fludd=[bytes([0x18, 0, 0, 0, 0, 4, 1, 65])] * 3),
                       mutate_section(teaching_file(fludd=[bytes(8)] * 3), 4, b'\x00\x01'),
                       mutate_section(teaching_file(), 48, struct.pack(">I", 100)),
                       mutate_section(teaching_file(), 15, b"\x07"),
                       mutate_section(teaching_file(), 32 + 3 * 16 + 12 + 10, b"\x00")]
                for data in bad:
                    self.assertEqual(decoder.validate(data, len(data)), 0)
            finally:
                handle = decoder._handle
                del decoder
                ctypes.windll.kernel32.FreeLibrary(ctypes.c_void_p(handle))

    def test_production_c_decoders_match_wire_fixtures(self):
        compiler = ROOT / "toolchain/clang.exe"
        if not compiler.exists():
            self.skipTest("bundled Windows compiler unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            source = folder / "decode.c"
            source.write_text('''#include "susamune/ghost_teaching.h"
__declspec(dllexport) int header(const unsigned char *p, unsigned int n) {
 return SusamuneGhostTeachingHeaderValid(p,n); }
__declspec(dllexport) int input(const unsigned char *p, unsigned int prior, int first) {
 return SusamuneGhostTeachingInputValid(p,100,104,prior,first); }
__declspec(dllexport) int split(const unsigned char *p, unsigned int prior, unsigned int endpoint) {
 return SusamuneGhostTeachingSplitValid(p,100,104,prior,0,0x1AF7E430,endpoint); }
''')
            library = folder / "decode.dll"
            subprocess.run([str(compiler), "--target=x86_64-pc-windows-msvc",
                            "-shared", "-nostdlib", "-fuse-ld=lld", "-Wl,/noentry",
                            "-I", str(ROOT / "include"), str(source), "-o", str(library)],
                           check=True, capture_output=True)
            decoder = ctypes.CDLL(str(library))
            section = teaching_file()[0x920:]
            self.assertTrue(decoder.header(section, len(section)))
            self.assertFalse(decoder.header(section, len(section) - 1))
            self.assertTrue(decoder.input(section[32:], 0, 1))
            self.assertFalse(decoder.input(section[32:], 100, 0))
            self.assertTrue(decoder.split(section[80:], 0, 0))
            self.assertFalse(decoder.split(section[80:], 0, 1))
            # Unload the DLL before TemporaryDirectory removes it on Windows.
            handle = decoder._handle
            del decoder
            ctypes.windll.kernel32.FreeLibrary(ctypes.c_void_p(handle))


if __name__ == "__main__":
    unittest.main()
