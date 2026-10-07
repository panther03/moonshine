"""Relay real ARM/PPC storage requests, replies and catalogue pages end to end.

Both production services run in their existing native host fixtures. Transport
structs therefore share native host byte order, while canonical files retain
their checked big-endian representation. Rendering and recording are mocked;
the production storage state machines, metadata validation and file I/O run.
"""

import ctypes
import struct
import unittest

import test_ghost_kernel_paging as arm_fixture
import test_ghost_storage_paging as ppc_fixture
from test_ghost_format import build_ghost
from test_ghost_storage import envelope
from test_ghost_teaching import teaching_file
from validate_ghost import validate_ghost


ARM_BRIDGE = r'''
__declspec(dllexport) int receiveRequest(const void *request,const void *data,u32 size) {
 if(size>SUSAMUNE_GHOST_STORAGE_PAYLOAD_SIZE) return 0;
 memcpy(&testMailbox.request,request,sizeof(testMailbox.request));
 if(testMailbox.request.payloadSize!=size) return 0;
 if(size)memcpy(testPayload,data,size);
 readBytes=readCalls=dirCalls=maxRead=0;
 return 1;
}
'''

PPC_BRIDGE = r'''
EXPORT clientReset() { reset();return 1; }
EXPORT clientRefresh(int imported,u32 offset) { return refreshPage(imported!=0,offset); }
EXPORT clientSave(const u8 *data,u32 size) {
 canonicalExport=data;canonicalExportSize=size;return saveNew(42);
}
EXPORT clientLoad(int imported,int row) {
 Identity selected;return copyIdentity(imported!=0,row,&selected)&&load(selected);
}
EXPORT clientTick() { update();return busy(); }
EXPORT clientReady(int imported) { return imported?importedCatalogReady():catalogReady(); }
EXPORT clientLoaded() { return playbackImports==1&&pinned; }
EXPORT clientSaved() { return savedToken==42&&checkpoints==1; }
extern "C" __declspec(dllexport) const void *clientRequest() { return &mailbox.request; }
extern "C" __declspec(dllexport) const void *clientPayload() { return payload; }
extern "C" __declspec(dllexport) const void *clientPage(int imported) { return &pages[imported!=0]; }
extern "C" __declspec(dllexport) const char *clientStatus() { return statusText(); }
EXPORT receiveResponse(const void *response,const void *data,u32 size) {
 if(size>SUSAMUNE_GHOST_STORAGE_PAYLOAD_SIZE)return 0;
 memcpy(&mailbox.response,response,sizeof(mailbox.response));
 if(mailbox.response.payloadSize!=size)return 0;
 if(size)memcpy(payload,data,size);
 update();return !busy();
}
'''


def canonical(version, identity):
    base = build_ghost(version=min(version, 4), ghost_id=identity + 1)
    if version == 5:
        base = teaching_file(base=base)
    elif version == 6:
        base = teaching_file(base=base, fludd=[bytes(8)] * 3)
    validate_ghost(base)
    return base


class GhostCatalogRoundtripTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Extend the fixture exports without copying either production source
        # or registering the fixtures' own test methods a second time.
        for name, fixture, bridge in (
            ("arm", arm_fixture.GhostKernelPagingTests, ARM_BRIDGE),
            ("ppc", ppc_fixture.GhostStoragePagingTests, PPC_BRIDGE),
        ):
            derived = type("Roundtrip" + name, (fixture,), {"bridge_source": bridge})
            derived.setUpClass()
            cls.addClassCleanup(derived.doClassCleanups)
            setattr(cls, name, derived.dll)
        cls.arm.receiveRequest.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint]
        cls.ppc.receiveResponse.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint]
        cls.ppc.clientSave.argtypes = [ctypes.c_void_p, ctypes.c_uint]
        for name in ("clientRequest", "clientPayload", "clientPage"):
            getattr(cls.ppc, name).restype = ctypes.c_void_p
        cls.ppc.clientStatus.restype = ctypes.c_char_p

    def setUp(self):
        self.arm.reset()
        self.ppc.clientReset()
        self.buffers = []

    def add(self, path, data):
        buffer = ctypes.create_string_buffer(data)
        self.buffers.append(buffer)
        self.assertGreaterEqual(self.arm.add(path.encode(), buffer, len(data)), 0)

    def file(self, path):
        size = ctypes.c_uint()
        address = self.arm.fileBytes(path.encode(), ctypes.byref(size))
        return ctypes.string_at(address, size.value) if address else None

    def relay(self, command, *, cached=False):
        request = self.ppc.clientRequest()
        fields = struct.unpack("<IHHIHHIIII", ctypes.string_at(request, 32))
        self.assertEqual(fields[2], command)
        self.assertEqual(fields[8], int(cached))
        self.assertEqual(self.arm.receiveRequest(request, self.ppc.clientPayload(), fields[7]), 1)
        passes = self.arm.run(1000000)
        self.assertGreaterEqual(passes, 0, "ARM exceeded its per-pass I/O budget or stalled")
        response = self.arm.response()
        reply = struct.unpack("<IHHIiIIIHH", ctypes.string_at(response, 32))
        self.assertEqual(reply[3], fields[3], "receipt must acknowledge the unchanged PPC sequence")
        self.assertEqual(reply[4], 0)
        result = ctypes.string_at(self.arm.payload(), reply[5])
        self.assertEqual(self.ppc.receiveResponse(response, self.arm.payload(), reply[5]), 1)
        if command == 4:
            self.assertEqual(self.ppc.clientReady(fields[4] == 4), 1, self.ppc.clientStatus())
            self.assertEqual(ctypes.string_at(self.ppc.clientPage(fields[4] == 4), len(result)), result)
        if cached:
            self.assertEqual(self.arm.bytesRead(), 0)
            self.assertEqual(passes, 1)
        return result

    def test_saved_v6_joins_legacy_catalog_and_pages_both_directions(self):
        originals = {}
        for slot in range(20):
            path = arm_fixture.PERSONAL + f"g{slot:02}a.sgh"
            data = envelope(canonical(3 + slot % 3, slot), slot=slot)
            originals[path] = data
            self.add(path, data)
        self.assertEqual(self.ppc.clientRefresh(0, 0), 1)
        self.relay(4)
        self.assertEqual(self.ppc.clientRefresh(0, 16), 1)
        self.relay(4, cached=True)

        saved = canonical(6, 99)
        self.assertEqual(self.ppc.clientSave(saved, len(saved)), 1)
        self.relay(1)
        self.assertEqual(self.ppc.clientSaved(), 1)
        self.assertEqual(self.ppc.clientReady(0), 0)
        self.assertEqual(self.file(arm_fixture.PERSONAL + "g20a.sgh")[64:], saved)

        self.assertEqual(self.ppc.clientTick(), 1)
        page = self.relay(4)
        self.assertGreater(self.arm.bytesRead(), 0, "saving must force a fresh catalogue")
        self.assertEqual(struct.unpack_from("<I", page, 12)[0], 21)
        self.assertEqual(struct.unpack_from("<I", page, 32 + 4 * 228)[0], 20)
        for offset in (0, 16, 0, 16):
            self.assertEqual(self.ppc.clientRefresh(0, offset), 1)
            self.relay(4, cached=True)
        self.assertEqual(self.ppc.clientLoad(0, 4), 1)
        self.assertEqual(self.relay(2), saved)
        self.assertEqual(self.ppc.clientLoaded(), 1)
        for path, data in originals.items():
            self.assertEqual(self.file(path), data)

    def test_imported_mixed_versions_use_identical_checked_pages(self):
        files = []
        for row in range(20):
            data = canonical(3 + row % 4, row)
            files.append(data)
            self.add(arm_fixture.IMPORT + f"mixed_{row:02}.smsghost", data)
        self.assertEqual(self.ppc.clientRefresh(1, 0), 1)
        page = self.relay(4)
        self.assertEqual(struct.unpack_from("<I", page, 12)[0], 20)
        for offset in (16, 0, 16):
            self.assertEqual(self.ppc.clientRefresh(1, offset), 1)
            self.relay(4, cached=True)
        self.assertEqual(self.ppc.clientLoad(1, 3), 1, self.ppc.clientStatus())
        self.assertEqual(self.relay(2), files[19])
        self.assertEqual(self.ppc.clientLoaded(), 1)
        self.assertEqual(self.arm.filesWritten(), 0)


if __name__ == "__main__":
    unittest.main()
