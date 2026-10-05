"""Drive the production ARM ghost service through a memory-backed FatFS."""
import ctypes
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest

from test_ghost_format import build_ghost
from test_ghost_storage import envelope
import validate_ghost as ghost_format
import validate_ghost_storage as storage

ROOT = Path(__file__).resolve().parents[1]
AUTO = 0xFFFFFFFF
PERSONAL = "/Moonshine data/ghosts/jp/p0/"
IMPORT = "/Moonshine data/ghosts/import/"

EXPORTS = r'''
__declspec(dllexport) void reset(void) {
 testStoragePrefix=MOONSHINE_DATA_ROOT;
 testCount=writeCount=readBytes=readCalls=dirCalls=maxRead=writeBytes=openCalls=0;
 failWriteAfter=0xFFFFFFFFu; failSync=false; directoryResult=FR_OK;
 memset(testFiles,0,sizeof(testFiles)); memset(&CatalogStorage,0,sizeof(CatalogStorage));
 memset(testPayload,0,sizeof(testPayload)); SusamuneGhostInit();
}
__declspec(dllexport) void volume(u32 usb) {
 testStoragePrefix=usb?"1:" MOONSHINE_DATA_ROOT:MOONSHINE_DATA_ROOT;
}
__declspec(dllexport) int add(const char *path,const u8 *bytes,u32 size) {
 int i=lookup(path); if(i<0) i=(int)testCount++;
 if(i>=FIXTURE_FILES) return -1;
 copystr(testFiles[i].path,path); testFiles[i].bytes=bytes;
 testFiles[i].size=size; testFiles[i].live=1; testFiles[i].writable=0; return i;
}
__declspec(dllexport) const u8 *fileBytes(const char *path,u32 *size) {
 int i=lookup(path); if(i<0) { *size=0; return 0; }
 *size=testFiles[i].size; return testFiles[i].bytes;
}
__declspec(dllexport) void submit(u32 command,u32 profile,u32 slot,const u8 *payload,u32 size,u32 generation) {
 memset(&testMailbox.request,0,32); testMailbox.request.requestMagic=SUSAMUNE_GHOST_STORAGE_MAGIC;
 testMailbox.request.protocolVersion=SUSAMUNE_GHOST_STORAGE_VERSION;
 testMailbox.request.command=command; testMailbox.request.profile=profile;
 testMailbox.request.slot=slot; testMailbox.request.payloadSize=size;
 testMailbox.request.expectedGeneration=generation; testMailbox.request.requestSeq=GhostAckSeq+1;
 if(size) memcpy(testPayload,payload,size);
 readBytes=readCalls=dirCalls=maxRead=openCalls=0;
}
__declspec(dllexport) void requestFlags(u32 flags) { testMailbox.request.flags=flags; }
__declspec(dllexport) int run(u32 limit) {
 for(u32 i=0;i<limit;++i) {
  u32 before=readBytes,dirs=dirCalls;
  if(!SusamuneGhostPending()) return (int)i;
  SusamuneGhostService();
  if(readBytes-before>SUSAMUNE_GHOST_STORAGE_CHUNK_SIZE || dirCalls-dirs>1) return -2;
 }
 return SusamuneGhostPending()?-1:0;
}
__declspec(dllexport) const void *response(void) { return &testMailbox.response; }
__declspec(dllexport) const void *payload(void) { return testPayload; }
__declspec(dllexport) u32 maximumRead(void) { return maxRead; }
__declspec(dllexport) u32 bytesRead(void) { return readBytes; }
__declspec(dllexport) u32 reads(void) { return readCalls; }
__declspec(dllexport) u32 opens(void) { return openCalls; }
__declspec(dllexport) u32 directoryReads(void) { return dirCalls; }
__declspec(dllexport) u32 filesWritten(void) { return writeCount; }
__declspec(dllexport) void missingDirectory(void) { directoryResult=FR_NO_PATH; }
__declspec(dllexport) void writeFailure(u32 after,u32 sync) { failWriteAfter=after; writeBytes=0; failSync=sync!=0; }
__declspec(dllexport) u64 aggregate(u32 count,u32 duration) {
 memset(&Page,0,sizeof(Page)); Page.first=0xFFFFFFFFu; PageOrdinal=0; PageDuration=0;
 memset(&ImportCandidate,0,sizeof(ImportCandidate)); ImportCandidate.info.durationQf=duration;
 for(u32 i=0;i<count;++i) InsertImportCandidate();
 PublishPage(); return ((u64)Page.totalDurationQfHi<<32)|Page.totalDurationQfLo;
}
'''


class GhostKernelPagingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / "toolchain/clang.exe"
        if sys.platform != "win32" or not compiler.exists():
            raise unittest.SkipTest("Bundled Windows compiler required")
        cls.temp = tempfile.TemporaryDirectory(prefix="moonshine-kernel-page-")
        cls.addClassCleanup(cls.temp.cleanup)
        path = Path(cls.temp.name)
        source = (ROOT / "launcher/kernel/SusamuneGhost.c").read_text()
        source = re.sub(r'^#include .*$', '', source, flags=re.M)
        (path / "kernel.c").write_text(
            '#include "ghost_kernel_fixture.h"\n' + source + EXPORTS +
            getattr(cls, "bridge_source", ""))
        result = subprocess.run([
            str(compiler), "--target=x86_64-pc-windows-msvc", "-shared", "-O1",
            "-fno-builtin", "-nostdlib", "-fuse-ld=lld", "-Xlinker", "/noentry",
            "-I", str(ROOT / "include"), "-I", str(ROOT / "scripts"),
            str(path / "kernel.c"), "-o", str(path / "kernel.dll")],
            capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        cls.dll = ctypes.CDLL(str(path / "kernel.dll"))
        cls.addClassCleanup(lambda: ctypes.windll.kernel32.FreeLibrary(
            ctypes.c_void_p(cls.dll._handle)))
        cls.dll.add.argtypes = [ctypes.c_char_p, ctypes.c_void_p, ctypes.c_uint]
        cls.dll.submit.argtypes = [ctypes.c_uint] * 3 + [ctypes.c_void_p] + [ctypes.c_uint] * 2
        cls.dll.run.argtypes = [ctypes.c_uint]
        cls.dll.fileBytes.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint)]
        for name in ("response", "payload", "fileBytes"):
            getattr(cls.dll, name).restype = ctypes.c_void_p
        cls.dll.aggregate.argtypes = [ctypes.c_uint, ctypes.c_uint]
        cls.dll.aggregate.restype = ctypes.c_ulonglong
        cls.dll.writeFailure.argtypes = [ctypes.c_uint, ctypes.c_uint]

    def setUp(self):
        self.dll.reset()
        self.buffers = []

    def add(self, path, raw):
        buffer = ctypes.create_string_buffer(raw)
        self.buffers.append(buffer)
        self.assertGreaterEqual(self.dll.add(path.encode(), buffer, len(raw)), 0)

    def file(self, path):
        size = ctypes.c_uint()
        ptr = self.dll.fileBytes(path.encode(), ctypes.byref(size))
        return ctypes.string_at(ptr, size.value) if ptr else None

    def request(self, command, slot=0, *, profile=0, payload=b"", generation=0, flags=0):
        self.dll.submit(command, profile, slot, payload, len(payload), generation)
        self.dll.requestFlags(flags)
        self.passes = self.dll.run(1000000)
        self.assertGreaterEqual(self.passes, 0, "state machine stalled or exceeded its per-pass I/O budget")
        result = struct.unpack("<IHHIiIIIHH", ctypes.string_at(self.dll.response(), 32))
        self.assertEqual(result[1], 5)
        self.assertEqual(result[2] & 2, 0)
        if result[4] != 0:
            self.assertEqual((result[5], result[8]), (0, 0))
        if command in (4, 6):
            self.assertEqual(result[7], 0)
        return {"status": result[4], "size": result[5], "generation": result[6],
                "slot": result[7], "count": result[8]}

    def page(self, offset=0, *, profile=0, cached=False):
        result = self.request(4, offset, profile=profile, flags=int(cached))
        self.assertEqual(result["status"], 0)
        raw = ctypes.string_at(self.dll.payload(), result["size"])
        self.assertEqual(len(raw), 3680)
        magic, version, count, first, total, lo, hi, next_slot, flags = struct.unpack_from("<IHH6I", raw)
        self.assertEqual((magic, version, first, flags), (0x53475047, 1, offset, 0))
        self.assertEqual(count, min(16, max(0, total - offset)))
        self.assertEqual(raw[32 + count * 228:], bytes((16-count)*228))
        rows = []
        for i in range(count):
            start = 32 + i * 228
            slot, generation = struct.unpack_from("<II", raw, start)
            row_flags = struct.unpack_from("<H", raw, start+4+40)[0]
            leaf = raw[start+132:start+228].split(b"\0")[0].decode()
            rows.append((slot, generation, leaf, row_flags))
        return rows, total, (hi << 32) | lo, next_slot

    def test_legacy_banks_page_past_45_and_preserve_reserved_ids(self):
        ghost = build_ghost()
        ids = list(range(45)) + list(range(48, 67))
        for slot in ids + [45, 46, 47]:
            self.add(PERSONAL + f"g{slot:02}a.sgh", envelope(ghost, slot=slot, version=1))
        self.add(PERSONAL + "g00b.sgh", envelope(ghost, generation=2, version=1))
        seen = []
        for offset in range(0, len(ids), 16):
            rows, total, duration, _ = self.page(offset)
            self.assertEqual(total, len(ids))
            self.assertEqual(duration, len(ids) * 4)
            self.assertLessEqual(self.dll.maximumRead(), 320)
            seen.extend(row[0] for row in rows)
        self.assertEqual(seen, ids)
        self.assertEqual(self.page(0)[0][0][1], 2)
        self.assertEqual(self.page(0xFFFFFFF0)[0], [])

    def test_save_auto_handles_wide_ids_and_preserves_payload(self):
        old, new = build_ghost(), build_ghost(ghost_id=99)
        legacy = envelope(old, slot=44, version=1)
        self.add(PERSONAL + "g44a.sgh", legacy)
        result = self.request(1, AUTO, payload=new)
        self.assertEqual((result["status"], result["slot"]), (0, 48))
        self.assertEqual(self.file(PERSONAL + "g44a.sgh"), legacy)
        self.add(PERSONAL + "g70000a.sgh", envelope(old, slot=70000))
        result = self.request(1, AUTO, payload=new)
        self.assertEqual((result["status"], result["slot"]), (0, 70001))
        saved = self.file(PERSONAL + "g70001a.sgh")
        self.assertEqual(saved[64:], new)
        self.assertEqual(struct.unpack_from(">I", saved, 40)[0], 70001)
        storage.validate_slot_file(saved, game_id=ghost_format.REGION_GAME_IDS[0], profile=0, slot=70001)

    def test_total_over_ten_hours_and_wide_aggregate_are_allowed(self):
        duration = ghost_format.MAX_DURATION_QF
        samples = [(160,320,480,0,0,0xC3,0), (160,320,480,0,60000,0xC3,0),
                   (160,320,480,0,duration-60000,0xC3,0)]
        ghost = build_ghost(samples=samples)
        ghost_format.validate_ghost(ghost)
        for slot in list(range(45)) + [48,49]:
            self.add(PERSONAL + f"g{slot:02}a.sgh", envelope(ghost, slot=slot))
        result = self.request(1, AUTO, payload=ghost)
        self.assertEqual(result["status"], 0)
        self.assertEqual(self.page()[2], 48 * duration)
        self.assertEqual(self.dll.aggregate(60000, duration), 60000 * duration)
        self.assertGreater(60000 * duration, 0xFFFFFFFF)

    def test_imports_have_multiple_pages_and_leaf_identity_survives_reorder(self):
        ghost = build_ghost()
        crc = struct.unpack_from(">I", ghost, 12)[0]
        for i in range(39):
            self.add(IMPORT + f"ghost_{i:02}.smsghost", ghost)
        leaves = []
        for offset in (0,16,32):
            rows, total, duration, _ = self.page(offset, profile=4)
            self.assertEqual((total,duration), (39,39*4))
            self.assertTrue(all(row[0] == 0 for row in rows))
            leaves.extend(row[2] for row in rows)
        self.assertEqual(len(set(leaves)), 39)
        leaf = b"ghost_35.smsghost".ljust(96,b"\0")
        self.assertEqual(self.request(2, profile=4, payload=leaf, generation=crc)["status"], 0)
        self.assertEqual(ctypes.string_at(self.dll.payload(), len(ghost)), ghost)
        changed = build_ghost(ghost_id=2)
        self.add(IMPORT + "ghost_35.smsghost", changed)
        self.assertEqual(self.request(3, profile=4, payload=leaf, generation=crc)["status"], -4)
        self.assertEqual(self.file(IMPORT + "ghost_35.smsghost"), changed)
        missing = b"missing.smsghost".ljust(96,b"\0")
        self.assertEqual(self.request(2, profile=4, payload=missing)["status"], -9)

    def test_maximum_import_leaf_roundtrips_under_both_data_volume_prefixes(self):
        ghost = build_ghost()
        crc = struct.unpack_from(">I", ghost, 12)[0]
        leaf = "x" * (95 - len(".smsghost")) + ".smsghost"
        for volume in (0, 1):
            with self.subTest(volume=volume):
                self.dll.reset()
                self.dll.volume(volume)
                path = ("1:" if volume else "") + IMPORT + leaf
                self.assertEqual(len(path), 127 if volume else 125)
                self.add(path, ghost)
                rows, total, _, _ = self.page(profile=4)
                self.assertEqual((total, rows[0][2]), (1, leaf))
                result = self.request(2, profile=4,
                    payload=leaf.encode().ljust(96, b"\0"), generation=crc)
                self.assertEqual(result["status"], 0)
                self.assertEqual(ctypes.string_at(self.dll.payload(), len(ghost)), ghost)

    def test_partial_write_and_out_of_space_preserve_previous_bank(self):
        ghost = build_ghost()
        old = envelope(ghost, generation=3, version=1)
        self.add(PERSONAL + "g00a.sgh", old)
        self.dll.writeFailure(32, 0)
        result = self.request(3, generation=3)
        self.assertNotEqual(result["status"], 0)
        self.assertEqual(self.file(PERSONAL + "g00a.sgh"), old)
        self.assertEqual(self.page()[0][0][1], 3)
        self.dll.writeFailure(64+100, 0)
        result = self.request(1, AUTO, payload=ghost)
        self.assertNotEqual(result["status"], 0)
        rows, total, _, _ = self.page()
        self.assertEqual(total, 2)
        self.assertTrue(rows[1][3] & 2)
        self.dll.writeFailure(AUTO, 0)
        self.assertEqual(self.request(2, generation=3)["status"], 0)

    def test_stale_personal_identity_and_corrupt_banks_fail_safely(self):
        ghost = build_ghost()
        old = envelope(ghost, generation=4, version=1)
        corrupt = bytearray(envelope(ghost, generation=5))
        corrupt[-1] ^= 1
        self.add(PERSONAL + "g00a.sgh", old)
        self.add(PERSONAL + "g00b.sgh", bytes(corrupt))
        self.assertEqual(self.request(3, generation=4)["status"], -4)
        loaded = self.request(2, generation=5)
        self.assertEqual(loaded["status"], -4)
        self.assertEqual(self.request(5, generation=5)["status"], -4)
        self.assertEqual(self.dll.filesWritten(), 0)
        self.assertEqual(self.file(PERSONAL + "g00a.sgh"), old)
        self.add(PERSONAL + "g01a.sgh", envelope(version=3, slot=1))
        rows, total, _, _ = self.page()
        self.assertEqual(total, 2)
        self.assertTrue(rows[1][3] & 2)
        self.assertEqual(self.request(3, 1)["status"], -8)

    def test_delete_frees_payload_and_save_reuses_tombstone_generation(self):
        ghost = build_ghost()
        self.add(PERSONAL + "g00a.sgh", envelope(ghost, generation=10, version=1))
        self.assertEqual(self.request(3, generation=10)["status"], 0)
        self.assertIsNone(self.file(PERSONAL + "g00a.sgh"))
        self.assertEqual(len(self.file(PERSONAL + "g00b.sgh")), 64)
        self.assertEqual(self.page()[1], 0)
        saved = self.request(1, AUTO, payload=ghost)
        self.assertEqual((saved["status"], saved["slot"], saved["generation"]), (0, 0, 12))
        self.assertEqual(self.file(PERSONAL + "g00a.sgh")[64:], ghost)
        self.assertEqual(self.request(2, generation=10)["status"], -4)

    def test_legacy_corrupt_header_recovers_valid_older_bank(self):
        ghost = build_ghost()
        self.add(PERSONAL + "g00a.sgh", envelope(ghost, generation=4, version=1))
        self.add(PERSONAL + "g00b.sgh", bytes(64) + ghost)
        self.assertEqual(self.page()[0][0][1], 4)
        self.assertEqual(self.request(2, generation=4)["status"], 0)

    def test_highest_id_hole_probe_preserves_corrupt_files(self):
        ghost = build_ghost()
        self.add(PERSONAL + "g00a.sgh", b"partial")
        self.add(PERSONAL + "g4294967294a.sgh", envelope(ghost, slot=0xFFFFFFFE))
        saved = self.request(1, AUTO, payload=ghost)
        self.assertEqual((saved["status"], saved["slot"]), (0, 1))
        self.assertEqual(self.file(PERSONAL + "g00a.sgh"), b"partial")

    def test_large_payload_load_is_chunked_and_listing_reads_only_headers(self):
        samples = [(160,320,480,0,0,0xC3,0)] + [(160,320,480,0,4,0xC3,0)] * 6000
        ghost = build_ghost(samples=samples)
        self.add(PERSONAL + "g00a.sgh", envelope(ghost))
        self.page()
        self.assertLessEqual(self.dll.bytesRead(), 1024)
        loaded = self.request(2, generation=1)
        self.assertEqual(loaded["status"], 0)
        self.assertEqual(self.dll.maximumRead(), 0x4000)
        self.assertEqual(ctypes.string_at(self.dll.payload(), len(ghost)), ghost)

    def test_failed_sync_keeps_last_committed_payload_and_reports_error(self):
        ghost = build_ghost()
        old = envelope(ghost, generation=8)
        self.add(PERSONAL + "g00a.sgh", old)
        self.page()
        self.dll.writeFailure(AUTO, 1)
        self.assertNotEqual(self.request(3, generation=8)["status"], 0)
        self.assertEqual(self.file(PERSONAL + "g00a.sgh"), old)
        self.dll.reset()
        self.add(PERSONAL + "g00a.sgh", old)
        self.dll.writeFailure(AUTO, 1)
        self.assertNotEqual(self.request(1, AUTO, payload=ghost)["status"], 0)
        self.assertEqual(self.file(PERSONAL + "g00a.sgh"), old)
        self.assertEqual(self.request(4, profile=5)["status"], -2)

    def test_missing_storage_directory_returns_clean_unavailable_response(self):
        self.add(PERSONAL + "g00a.sgh", envelope(build_ghost()))
        self.page()
        self.dll.missingDirectory()
        self.assertEqual(self.request(4)["status"], -7)
        self.assertEqual(self.request(6, profile=4)["status"], -7)
        self.assertEqual(self.request(1, AUTO, payload=build_ghost())["status"], -7)
        self.assertEqual(self.dll.filesWritten(), 0)

    def test_zero_wrapped_generation_is_a_valid_personal_identity(self):
        self.add(PERSONAL + "g00a.sgh", envelope(build_ghost(), generation=0))
        self.assertEqual(self.page()[0][0][1], 0)
        self.assertEqual(self.request(2, generation=0)["status"], 0)

    def test_adjacent_pages_use_no_disk_reads_and_do_not_limit_library(self):
        ghost = build_ghost()
        ids = list(range(45)) + list(range(48, 83))
        for slot in ids:
            self.add(PERSONAL + f"g{slot:02}a.sgh", envelope(ghost, slot=slot))
        self.page()
        self.assertEqual(self.dll.reads(), len(ids))
        self.assertEqual(self.dll.bytesRead(), len(ids) * 320)
        for offset in (16, 32, 48, 0):
            rows, total, _, _ = self.page(offset, cached=True)
            self.assertEqual([row[0] for row in rows], ids[offset:offset+16])
            self.assertEqual(total, len(ids))
            self.assertEqual((self.dll.reads(), self.dll.opens(), self.dll.directoryReads()), (0, 0, 0))
            self.assertEqual(self.passes, 1)
        self.assertEqual([row[0] for row in self.page(64, cached=True)[0]], ids[64:])
        self.assertGreater(self.dll.reads(), 0)
        self.assertEqual(self.page(64, cached=True)[1], len(ids))
        self.assertEqual(self.dll.reads(), 0)
        self.page(48, cached=True)
        self.assertGreater(self.dll.reads(), 0)

    def test_personal_and_import_cache_are_independent_but_profile_is_checked(self):
        ghost = build_ghost()
        for slot in range(35):
            self.add(PERSONAL + f"g{slot:02}a.sgh", envelope(ghost, slot=slot))
            self.add(IMPORT + f"ghost{slot}.smsghost", ghost)
        self.page()
        self.page(profile=4)
        for profile in (0, 4):
            self.assertEqual(self.page(16, profile=profile, cached=True)[1], 35)
            self.assertEqual(self.dll.opens(), 0)
        self.assertEqual(self.page(0, profile=1, cached=True)[1], 0)
        self.assertGreater(self.dll.directoryReads(), 0)
        self.assertEqual(self.page(16, cached=True)[1], 35)
        self.assertGreater(self.dll.opens(), 0)
        self.page(16, profile=4, cached=True)
        self.assertEqual(self.dll.opens(), 0)

    def test_refresh_and_mutations_invalidate_cached_metadata(self):
        ghost = build_ghost()
        self.add(PERSONAL + "g00a.sgh", envelope(ghost))
        self.page()
        self.add(PERSONAL + "g01a.sgh", envelope(ghost, slot=1))
        self.assertEqual(self.page(cached=True)[1], 1)
        self.assertEqual(self.dll.reads(), 0)
        self.assertEqual(self.page()[1], 2)
        self.assertGreater(self.dll.reads(), 0)
        self.assertEqual(self.request(1, AUTO, payload=ghost)["status"], 0)
        self.assertEqual(self.page(cached=True)[1], 3)
        self.assertGreater(self.dll.reads(), 0)
        self.assertEqual(self.request(3, 1, generation=1)["status"], 0)
        self.assertEqual(self.page(cached=True)[1], 2)
        self.assertGreater(self.dll.reads(), 0)

    def test_failed_mutation_and_scan_never_publish_a_stale_or_partial_cache(self):
        ghost = build_ghost()
        self.add(PERSONAL + "g00a.sgh", envelope(ghost))
        self.page()
        self.dll.writeFailure(64+100, 0)
        self.assertNotEqual(self.request(1, AUTO, payload=ghost)["status"], 0)
        rows, total, _, _ = self.page(cached=True)
        self.assertEqual(total, 2)
        self.assertTrue(rows[1][3] & 2)
        self.assertGreater(self.dll.reads(), 0)
        self.dll.missingDirectory()
        self.assertEqual(self.request(4)["status"], -7)
        self.assertEqual(self.request(4, flags=1)["status"], -7)

    def test_cached_import_identity_cannot_authorize_loading_or_deleting_a_replacement(self):
        ghost = build_ghost()
        self.add(IMPORT + "example.smsghost", ghost)
        rows, _, _, _ = self.page(profile=4)
        changed = build_ghost(ghost_id=123)
        self.add(IMPORT + "example.smsghost", changed)
        leaf = b"example.smsghost".ljust(96, b"\0")
        for command in (2, 3):
            self.assertEqual(self.request(command, profile=4, payload=leaf,
                generation=rows[0][1])["status"], -4)
            self.assertEqual(self.file(IMPORT + "example.smsghost"), changed)
        self.assertEqual(self.page(profile=4, cached=True)[0][0][1],
            struct.unpack_from(">I", changed, 12)[0])
        self.assertGreater(self.dll.reads(), 0)

    def test_cache_hint_is_list_only_and_unknown_flags_are_rejected(self):
        self.assertEqual(self.request(4, flags=2)["status"], -1)
        self.assertEqual(self.request(6, profile=4, flags=1)["status"], -1)
        for command in (1, 2, 3, 5):
            self.assertEqual(self.request(command, flags=1)["status"], -1)


if __name__ == "__main__":
    unittest.main()
