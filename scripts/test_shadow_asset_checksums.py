"""Exercise the launcher's production payload-validation block with zlib CRCs."""
import ctypes
from pathlib import Path
import random
import re
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[1]


class ShadowAssetChecksumTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        clang = ROOT / "toolchain/clang.exe"
        if sys.platform != "win32" or not clang.exists():
            raise unittest.SkipTest("Bundled Windows compiler required for native C fixture")
        cls.temp = tempfile.TemporaryDirectory(prefix="moonshine-asset-crc-")
        cls.addClassCleanup(cls.temp.cleanup)
        work = Path(cls.temp.name)
        production = (ROOT / "launcher/loader/source/SusamuneShadowAsset.c").read_text()
        spec = production[production.index("typedef struct AssetSpec {"):
                          production.index("} AssetSpec;") + len("} AssetSpec;")]
        read_be = production[production.index("static u16 ReadBE16("):
                             production.index("static bool RangeFits(")]
        reader_types = production[production.index("typedef struct ShadowInput {"):
                                  production.index("typedef struct FstEntry {")]
        bounds = production[production.index("static bool RangeFits("):
                            production.index("static void PublishStatus(")]
        decoder = production[production.index("static bool InputByte("):
                             production.index("static bool BuildExtractedPath(")]
        limits = production[production.index("#define SHADOW_INPUT_SIZE"):
                            production.index("typedef enum ShadowSourceKind")]
        block = production[production.index('if (memcmp((const void *)spec->payload, "J3D2bmd3"'):
                           production.index("\ndone:\n\treturn status;", production.index("static int ValidatePayload("))]
        constants = (ROOT / "include/susamune/ghost_model_asset.h").read_text()
        statuses = "\n".join(re.findall(
            r"^#define SUSAMUNE_GHOST_MODEL_STATUS_(?:RESOURCE_MISSING|BAD_CHECKSUM|BAD_YAZ0|BAD_RARC|READ_FAILED)\s+-\d+",
            constants, re.M))
        raw_reader = production[production.index("static bool ReaderRawRead("):
                                production.index("static bool ReaderLogicalRead(")]
        raw_reader = raw_reader.replace("ShadowReader", "DiscReader")
        source = work / "test.c"
        source.write_text('''
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef unsigned long uLong;
typedef unsigned char Bytef;
typedef _Bool bool;
#define true 1
#define false 0
#define NULL ((void *)0)
#define Z_NULL NULL
''' + statuses + '\n' + statuses.replace("MODEL_STATUS_", "SHADOW_STATUS_") + '\n' + limits + '''
static uLong (*crc32)(uLong, const Bytef *, u32);
static int memcmp(const void *a, const void *b, unsigned int size) {
    const u8 *x = a, *y = b;
    while (size--) if (*x++ != *y++) return 1;
    return 0;
}
static void *memset(void *p, int value, unsigned int size) {
    u8 *out = p;
    while (size--) *out++ = value;
    return p;
}
static unsigned int strlen(const char *s) {
    unsigned int size = 0;
    while (s[size]) ++size;
    return size;
}
typedef struct ShadowReader { const u8 *data; u32 size; } ShadowReader;
static u32 reads, largestRead, bytesRead;
static bool ReaderRead(ShadowReader *reader, u64 offset, void *data, u32 size) {
    u8 *out = data;
    if (offset > reader->size || size > reader->size - offset) return false;
    reads++; bytesRead += size;
    if (size > largestRead) largestRead = size;
    while (size--) *out++ = reader->data[offset++];
    return true;
}
''' + spec + reader_types + read_be + bounds + decoder + '''
static ShadowDecoder decoderState;
__declspec(dllexport) int decode(const u8 *input, u32 size, u8 *payload,
    u32 bmdSize, u32 btkSize) {
    ShadowReader reader;
    AssetSpec asset;
    int failure = 0;
    reader.data = input;
    reader.size = size;
    reads = largestRead = bytesRead = 0;
    asset.payload = payload;
    asset.nodeName = "node";
    asset.bmdName = "model.bmd";
    asset.btkName = btkSize ? "track.btk" : NULL;
    asset.bmdSize = bmdSize;
    asset.btkSize = btkSize;
    return DecodeArchive(&reader, 0, size, &asset, &decoderState, &failure)
        ? 0 : failure;
}
__declspec(dllexport) u32 read_count(void) { return reads; }
__declspec(dllexport) u32 read_largest(void) { return largestRead; }
__declspec(dllexport) u32 read_bytes(void) { return bytesRead; }
__declspec(dllexport) void set_crc(uLong (*callback)(uLong,const Bytef *,u32)) {
    crc32 = callback;
}
__declspec(dllexport) int validate(u8 *payload, u32 bmdSize, u32 btkSize,
    u32 payloadChecksum, u32 bmdChecksumExpected, u32 btkChecksumExpected) {
    AssetSpec asset;
    const AssetSpec *spec = &asset;
    uLong checksum, bmdChecksum, btkChecksum;
    int status = 0;
    asset.payload = payload;
    asset.bmdSize = bmdSize;
    asset.btkSize = btkSize;
    asset.payloadChecksum = payloadChecksum;
    asset.bmdChecksum = bmdChecksumExpected;
    asset.btkChecksum = btkChecksumExpected;
''' + block + '''
done:
    return status;
}
''' + '''
typedef u32 UINT;
typedef u64 FSIZE_t;
#define FR_OK 0
#define SHADOW_SOURCE_FILE 1
typedef struct DiscReader { int kind; u32 file; u64 size; u32 discCommand; } DiscReader;
static u8 sDiscReadScratch[SHADOW_INPUT_SIZE + 0x20];
static u8 discOutput[SHADOW_INPUT_SIZE + 2];
static u64 diskOffset;
static u32 diskCalls, diskReadSize;
static int f_lseek(u32 *file, FSIZE_t offset) { diskOffset = offset; return FR_OK; }
static int f_read(u32 *file, void *output, UINT size, UINT *got) {
    u8 *out = output;
    for (u32 i = 0; i < size; ++i) out[i] = (u8)(diskOffset + i);
    diskCalls++; diskReadSize = size; *got = size; return FR_OK;
}
static void ReadRealDisc(u8 *out, u64 offset, u32 size, u32 command) {
    diskCalls++; diskOffset = offset; diskReadSize = size;
    for (u32 i = 0; i < size; ++i) out[i] = (u8)(offset + i);
}
static void *memcpy(void *output, const void *input, unsigned int size) {
    u8 *out = output; const u8 *in = input;
    while (size--) *out++ = *in++;
    return output;
}
''' + raw_reader + '''
__declspec(dllexport) int input_alignment(int file, u32 offset, u32 size) {
    DiscReader reader = {file ? SHADOW_SOURCE_FILE : 2, 0, 0x20000, 0xA8};
    diskCalls = diskReadSize = 0;
    memset(discOutput, 0xA5, sizeof(discOutput));
    if (size > SHADOW_INPUT_SIZE) return 1;
    bool result = ReaderRawRead(&reader, offset, discOutput + 1, size);
    if (offset > reader.size || size > reader.size - offset)
        return !result && !diskCalls && discOutput[1] == 0xA5 ? 0 : 2;
    if (!result || diskCalls != 1) return 3;
    if (discOutput[0] != 0xA5 || discOutput[size + 1] != 0xA5) return 4;
    for (u32 i = 0; i < size; ++i)
        if (discOutput[i + 1] != (u8)(offset + i)) return 5;
    if (!file && ((diskOffset & 31) || (diskReadSize & 31) ||
                  diskReadSize > SHADOW_INPUT_SIZE + 32)) return 6;
    return 0;
}
''', encoding="ascii")
        library = work / "asset.dll"
        compiled = subprocess.run([str(clang), "--target=x86_64-pc-windows-msvc", "-shared",
                        "-nostdlib", "-fno-builtin", "-fuse-ld=lld", "-Xlinker",
                        "/noentry", str(source), "-o", str(library)],
                       capture_output=True, text=True)
        if compiled.returncode:
            raise RuntimeError(compiled.stdout + compiled.stderr)
        cls.dll = ctypes.CDLL(str(library))
        cls.addClassCleanup(lambda: ctypes.windll.kernel32.FreeLibrary(
            ctypes.c_void_p(cls.dll._handle)))
        callback_type = ctypes.CFUNCTYPE(ctypes.c_ulong, ctypes.c_ulong,
                                         ctypes.c_void_p, ctypes.c_uint)
        cls.crc_calls = []

        def crc(seed, data, size):
            cls.crc_calls.append((seed, size))
            return zlib.crc32(ctypes.string_at(data, size), seed) if data else 0

        cls.crc_callback = callback_type(crc)
        cls.dll.set_crc.argtypes = [callback_type]
        cls.dll.set_crc(cls.crc_callback)
        cls.dll.validate.argtypes = [ctypes.c_void_p] + [ctypes.c_uint] * 5
        cls.dll.validate.restype = ctypes.c_int
        cls.dll.decode.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p,
                                   ctypes.c_uint, ctypes.c_uint]
        cls.dll.decode.restype = ctypes.c_int

    @staticmethod
    def payload(bmd_size, btk_size):
        data = bytearray(random.Random(bmd_size).randbytes(bmd_size + btk_size))
        data[:8] = b"J3D2bmd3"
        struct.pack_into(">I", data, 8, bmd_size)
        if btk_size:
            data[bmd_size:bmd_size + 8] = b"J3D1btk1"
            struct.pack_into(">I", data, bmd_size + 8, btk_size)
        return data

    def validate(self, data, bmd_size, btk_size, expected=None):
        if expected is None:
            expected = (zlib.crc32(data), zlib.crc32(data[:bmd_size]),
                        zlib.crc32(data[bmd_size:]))
        self.crc_calls.clear()
        buffer = (ctypes.c_ubyte * len(data)).from_buffer_copy(data)
        return self.dll.validate(buffer, bmd_size, btk_size, *expected)

    def test_retail_size_payloads_keep_all_checks_and_avoid_second_bmd_pass(self):
        hashed = 0
        for bmd_size, btk_size in ((0xF8C0, 0x440), (0x119A0, 0)):
            with self.subTest(bmd=bmd_size, btk=btk_size):
                data = self.payload(bmd_size, btk_size)
                self.assertEqual(self.validate(data, bmd_size, btk_size), 0)
                self.assertEqual(sum(size for _, size in self.crc_calls),
                                 bmd_size + 2 * btk_size)
                if btk_size:
                    self.assertIn((zlib.crc32(data[:bmd_size]), btk_size),
                                  self.crc_calls)
                hashed += sum(size for _, size in self.crc_calls)
        self.assertEqual(hashed, 137952)
        self.assertEqual(2 * (0xF8C0 + 0x440 + 0x119A0) - hashed, 135776)

    def test_corruption_in_either_file_and_each_expected_crc_still_rejects(self):
        for bmd_size, btk_size in ((0xF8C0, 0x440), (0x119A0, 0)):
            data = self.payload(bmd_size, btk_size)
            expected = (zlib.crc32(data), zlib.crc32(data[:bmd_size]),
                        zlib.crc32(data[bmd_size:]))
            for index in (12, bmd_size - 1, len(data) - 1):
                corrupt = data.copy()
                corrupt[index] ^= 1
                self.assertLess(self.validate(corrupt, bmd_size, btk_size, expected), 0)
            for index in range(3):
                corrupt_crc = list(expected)
                corrupt_crc[index] ^= 1
                self.assertLess(self.validate(data, bmd_size, btk_size, corrupt_crc), 0)

    def test_header_checks_reject_even_with_matching_corrupt_payload_crcs(self):
        bmd_size, btk_size = 0xF8C0, 0x440
        original = self.payload(bmd_size, btk_size)
        for index in (0, 8, bmd_size, bmd_size + 8):
            data = original.copy()
            data[index] ^= 1
            self.assertLess(self.validate(data, bmd_size, btk_size), 0)
            self.assertEqual(self.crc_calls, [])

    @staticmethod
    def archive(bmd_size=64, btk_size=32):
        data = bytearray(0x40000)
        names = b"node\0model.bmd\0track.btk\0"
        count = 2 if btk_size else 1
        struct.pack_into(">4s4I", data, 0, b"RARC", len(data), 0x20, 0xE0,
                         len(data) - 0x100)
        struct.pack_into(">6I", data, 0x20, 1, 0x20, count, 0x30, len(names), 0x70)
        struct.pack_into(">IIHHI", data, 0x40, 0x524F4F54, 0, 0, count, 0)
        struct.pack_into(">5I", data, 0x50, 0, 0x01000005, 0, bmd_size, 0)
        if btk_size:
            struct.pack_into(">5I", data, 0x64, 0, 0x0100000F, bmd_size, btk_size, 0)
        data[0x90:0x90 + len(names)] = names
        payload = ShadowAssetChecksumTests.payload(bmd_size, btk_size)
        data[0x100:0x100 + len(payload)] = payload
        return data, payload

    @staticmethod
    def yaz0(data, backrefs=False):
        tokens = []
        cursor = 0
        while cursor < len(data):
            run = 0
            if backrefs and cursor:
                while (run < 273 and cursor + run < len(data) and
                       data[cursor + run] == data[cursor - 1]):
                    run += 1
            if run >= 3:
                token = bytes((0, 0, run - 18)) if run >= 18 else bytes(((run - 2) << 4, 0))
                tokens.append((False, token))
                cursor += run
            else:
                tokens.append((True, bytes((data[cursor],))))
                cursor += 1
        encoded = bytearray(b"Yaz0" + struct.pack(">I", len(data)) + bytes(8))
        for offset in range(0, len(tokens), 8):
            group = tokens[offset:offset + 8]
            encoded.append(sum(0x80 >> i for i, (literal, _) in enumerate(group) if literal))
            for _, token in group:
                encoded.extend(token)
        return encoded

    def decode(self, encoded, bmd_size=64, btk_size=32):
        source = (ctypes.c_ubyte * len(encoded)).from_buffer_copy(encoded)
        guarded = (ctypes.c_ubyte * (bmd_size + btk_size + 2))()
        guarded[0] = guarded[-1] = 0xA5
        result = self.dll.decode(source, len(encoded), ctypes.byref(guarded, 1),
                                 bmd_size, btk_size)
        self.assertEqual((guarded[0], guarded[-1]), (0xA5, 0xA5))
        return result, bytes(guarded)[1:-1]

    def test_production_decoder_handles_literals_overlap_and_history_wrap(self):
        for btk_size in (0, 32):
            archive, payload = self.archive(btk_size=btk_size)
            for backrefs in (False, True):
                with self.subTest(btk=btk_size, backrefs=backrefs):
                    result, decoded = self.decode(self.yaz0(archive, backrefs), btk_size=btk_size)
                    self.assertEqual(result, 0)
                    self.assertEqual(decoded, payload)

    def test_streaming_reads_batch_full_archive_and_keep_final_short_read_bounded(self):
        archive, payload = self.archive()
        encoded = self.yaz0(archive)
        result, decoded = self.decode(encoded)
        self.assertEqual((result, decoded), (0, payload))
        self.assertEqual(self.dll.read_bytes(), len(encoded))
        self.assertEqual(self.dll.read_largest(), 0x8000)
        self.assertEqual(self.dll.read_count(), 1 + (len(encoded) - 16 + 0x7FFF) // 0x8000)
        self.assertLess(self.dll.read_count(), 1 + (len(encoded) - 16 + 0xFFF) // 0x1000)

    def test_file_and_disc_input_reads_preserve_alignment_and_bounds(self):
        for file in (0, 1):
            for offset in (0, 1, 15, 31, 0x1234):
                for size in (1, 31, 32, 0xFFF, 0x1000, 0x7FFF, 0x8000):
                    with self.subTest(file=file, offset=offset, size=size):
                        self.assertEqual(self.dll.input_alignment(file, offset, size), 0)
            self.assertEqual(self.dll.input_alignment(file, 0x1FFFF, 2), 0)
            self.assertEqual(self.dll.input_alignment(file, 0x20001, 1), 0)

    def test_production_decoder_rejects_truncation_invalid_backrefs_and_trailing_bytes(self):
        archive, _ = self.archive()
        encoded = self.yaz0(archive, True)
        bad_backref = bytearray(encoded)
        bad_backref[16:19] = bytes((0, 0x10, 0))
        oversized = bytearray(encoded)
        struct.pack_into(">I", oversized, 4, 0x600001)
        for damaged in (encoded[:15], encoded[:-1], encoded + b"\0", bad_backref, oversized):
            self.assertLess(self.decode(damaged)[0], 0)

    def test_production_decoder_rejects_bad_rarc_tables_and_asset_sizes(self):
        archive, _ = self.archive()
        for offset, value in ((0x0C, 0x10000), (0x24, 0xFFFF),
                              (0x28, 0x10000), (0x30, 0xFFFF), (0x5C, 65)):
            damaged = archive.copy()
            struct.pack_into(">I", damaged, offset, value)
            self.assertLess(self.decode(self.yaz0(damaged, True))[0], 0)


if __name__ == "__main__":
    unittest.main()
