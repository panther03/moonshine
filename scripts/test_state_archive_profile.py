"""Exercise production cold-boot owner admission and retained-byte filtering."""
import re
import ctypes as C
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CURRENT_BUILD = 0x4D530011
LEGACY_BUILDS = ((0x7D55E8F2, 0x8C8B60C4),
                 (0x46F87973, 0xD784A910),
                 (0xEA3C3DFD, 0x96DD313A))
U = C.c_uint
READ = C.CFUNCTYPE(C.c_bool, C.c_void_p, U, C.POINTER(U))
class Word(C.Structure):
    _fields_ = [('address',U),('value',U)]
class Range(C.Structure):
    _fields_ = [('address',U),('size',U)]
class Data(C.Structure):
    _fields_ = [('magic',U),('version',U),('game',U),('build',U),('config',U),
                ('count',U),('keepCount',U),('checksum',U),('anchors',Word*640),('keep',Range*96)]
class Layout(C.Structure):
    _fields_ = [(n,U) for n in ('game','application','rootHeap','systemHeap','currentHeap',
        'setupThread','setupThreadStack','volumeList','solidVtable','expVtable',
        'memArchiveVtable','aramArchiveVtable','timeRec','rumble')] + [('globals',U*16)]

class ArchiveProfileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=ROOT/'toolchain/clang++.exe'
        if not compiler.exists():raise unittest.SkipTest('Bundled Windows compiler required')
        cls.folder=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.folder.cleanup)
        shim=Path(cls.folder.name)/'profile.cpp'
        shim.write_text(r'''
#include "susamune/state_archive_profile.hxx"
#include "susamune/state_compatibility.h"
extern "C" {
void *memcpy(void *d,const void *s,__SIZE_TYPE__ n) {
 unsigned char *a=(unsigned char*)d;const unsigned char *b=(const unsigned char*)s;
 while(n--)*a++=*b++;return d;
}
void *memset(void *d,unsigned long v,__SIZE_TYPE__ n) {
 unsigned char *a=(unsigned char*)d;while(n--)*a++=(unsigned char)v;return d;
}
int memcmp(const void *a,const void *b,__SIZE_TYPE__ n) {
 const unsigned char *x=(const unsigned char*)a,*y=(const unsigned char*)b;
 while(n--){if(*x!=*y)return *x-*y;++x;++y;}return 0;
}
__declspec(dllexport) bool snapshot(StateArchiveProfile::Data *d,
 const StateArchiveProfile::Layout *l,StateArchiveProfile::ReadWord read,void *ctx) {
 return StateArchiveProfile::captureWithReader(*d,*l,read,ctx,SUSAMUNE_STATE_COMPATIBILITY_ID,23);
}
__declspec(dllexport) int admitted(unsigned game,unsigned build){return SusamuneStateBuildCompatible(game,build);}
__declspec(dllexport) bool valid(const StateArchiveProfile::Data *d){return StateArchiveProfile::valid(*d);}
__declspec(dllexport) bool match(const StateArchiveProfile::Data *a,const StateArchiveProfile::Data *b){return StateArchiveProfile::matches(*a,*b);}
__declspec(dllexport) bool reidentify(StateArchiveProfile::Data *d,unsigned build){return StateArchiveProfile::reidentify(*d,build);}
__declspec(dllexport) unsigned failure(){return StateArchiveProfile::failureAddress();}
__declspec(dllexport) void filtered(StateArchiveProfile::Data *d,void *out,const void *in,unsigned n){StateArchiveProfile::copyGameBytes(d,out,in,n);}
}
''')
        production = shim.with_name('profile-production.cpp')
        production.write_text(re.sub(r'^#pragma .*$', '',
            (ROOT/'src/state_archive_profile.cpp').read_text(), flags=re.M))
        dll=shim.with_suffix('.dll')
        subprocess.run([str(compiler),'--target=x86_64-pc-windows-msvc','-shared','-nostdlib',
            '-fuse-ld=lld','-Wl,/noentry','-O2','-fno-builtin','-mno-stack-arg-probe',
            '-DSTATE_ARCHIVE_PROFILE_HOST','-I',str(ROOT/'include'),str(shim),
            str(production),'-o',str(dll)],check=True)
        cls.lib=C.CDLL(str(dll))
        cls.addClassCleanup(lambda:C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))
        cls.lib.snapshot.argtypes=[C.POINTER(Data),C.POINTER(Layout),READ,C.c_void_p]
        cls.lib.snapshot.restype=C.c_bool
        cls.lib.valid.argtypes=[C.POINTER(Data)];cls.lib.valid.restype=C.c_bool
        cls.lib.match.argtypes=[C.POINTER(Data),C.POINTER(Data)];cls.lib.match.restype=C.c_bool
        cls.lib.reidentify.argtypes=[C.POINTER(Data),U];cls.lib.reidentify.restype=C.c_bool
        cls.lib.failure.restype=U
        cls.lib.filtered.argtypes=[C.POINTER(Data),C.c_void_p,C.c_void_p,U]

    def setUp(self):
        self.mem={};self.root=0x80500000;self.stage=0x80600000;self.end=0x81000000
        self.layout=Layout(2,0x80300000,0x80300100,0x80300104,0x80300108,
            0x80301000,0x8030010c,0x80300200,0x80200000,0x80200100,
            0x80200200,0x80200300,0x80300110,0x80300114)
        m=self.mem;l=self.layout
        m[l.rootHeap]=m[l.systemHeap]=self.root;m[l.currentHeap]=self.stage
        m[self.root]=l.expVtable;m[self.stage]=l.solidVtable
        for h in (self.root,self.stage):
            m[h+0x30]=h+0x90;m[h+0x34]=self.end;m[h+0x38]=self.end-h-0x90
        m[self.root+0x3c]=m[self.root+0x40]=self.stage+0x48;m[self.root+0x44]=1
        m[self.stage+0x48]=self.stage;m[self.stage+0x4c]=self.root+0x3c
        node=self.root+0x90;m[self.root+0x7c]=m[self.root+0x80]=node
        m[node]=0x484d0000;m[node+4]=self.end-node-16
        app=l.application
        for i,off in enumerate((0,4,0x1c,0x20,0x24,0x28,0x2c,0x30,0x34,0x40)):
            m[app+off]=self.root+0x2000+i*0x100
        m[app]=app;m[app+0x40]=self.stage
        m[l.timeRec]=self.root+0x6000;m[m[l.timeRec]]=0x80200600
        m[l.rumble]=self.root+0x7000;m[l.setupThreadStack]=self.root+0x8000
        self.volume=self.stage+0x1000;v=self.volume
        m[l.volumeList]=m[l.volumeList+4]=v+0x18;m[l.volumeList+8]=1
        m[v]=l.memArchiveVtable;m[v+0x18]=v;m[v+0x1c]=l.volumeList
        m[v+0x60]=self.stage+0x2000;m[m[v+0x60]]=0x52415243;m[m[v+0x60]+4]=0x100
        @READ
        def read(_,address,out):
            out[0]=self.mem.get(address,0);return True
        self.read=read

    def capture(self):
        d=Data();ok=self.lib.snapshot(C.byref(d),C.byref(self.layout),self.read,None)
        return ok,d

    def reseal(self, data):
        data.checksum = 0
        value = 2166136261
        for byte in bytes(data):
            value = ((value ^ byte) * 16777619) & 0xffffffff
        data.checksum = value

    def test_only_current_contract_and_exact_region_builds_are_admitted(self):
        for game in range(1, 4):
            for build in (CURRENT_BUILD, *LEGACY_BUILDS[game - 1]):
                self.assertTrue(self.lib.admitted(game, build), (game, hex(build)))
            for other in range(1, 4):
                if other != game:
                    for build in LEGACY_BUILDS[other - 1]:
                        self.assertFalse(self.lib.admitted(game, build))
            for build in (0, 17, 0xffffffff, 0xBD88B673, 0x0E7FDCB9, 0x2D9D231D,
                          0xC05B6660, 0x466C2034, 0x5363FD8B):
                self.assertFalse(self.lib.admitted(game, build), hex(build))
        for game in (0, 4, 255, 0xffffffff):
            self.assertFalse(self.lib.admitted(game, CURRENT_BUILD))

    def test_audited_builds_match_without_rewriting_either_profile(self):
        for game, builds in enumerate(LEGACY_BUILDS, 1):
            self.layout.game = game
            ok, live = self.capture()
            self.assertTrue(ok)
            for build in builds:
                saved = Data.from_buffer_copy(live)
                saved.build = build
                self.reseal(saved)
                before = bytes(saved), bytes(live)
                self.assertTrue(self.lib.match(C.byref(saved), C.byref(live)))
                self.assertTrue(self.lib.match(C.byref(live), C.byref(saved)))
                self.assertEqual((bytes(saved), bytes(live)), before)

    def test_build_compatibility_cannot_bypass_profile_checksum(self):
        _, live = self.capture()
        saved = Data.from_buffer_copy(live)
        saved.build = LEGACY_BUILDS[1][0]
        self.assertFalse(self.lib.match(C.byref(saved), C.byref(live)))
        self.reseal(saved)
        self.assertTrue(self.lib.match(C.byref(saved), C.byref(live)))
        live.checksum ^= 1
        self.assertFalse(self.lib.match(C.byref(saved), C.byref(live)))

    def test_reidentify_changes_only_build_and_derived_checksum(self):
        _, current = self.capture()
        for build in LEGACY_BUILDS[1]:
            legacy = Data.from_buffer_copy(current)
            legacy.build = build
            self.reseal(legacy)
            before = bytes(legacy)
            self.assertTrue(self.lib.reidentify(C.byref(legacy), CURRENT_BUILD))
            self.assertEqual(bytes(legacy), bytes(current))
            self.assertEqual(bytes(legacy)[:12], before[:12])
            self.assertEqual(bytes(legacy)[16:28], before[16:28])
            self.assertEqual(bytes(legacy)[32:], before[32:])
            self.assertTrue(self.lib.valid(C.byref(legacy)))

    def test_reidentify_refuses_unknown_wrong_region_or_corrupt_identity_without_mutation(self):
        _, current = self.capture()
        for source, target, corrupt in (
            (17, CURRENT_BUILD, False),
            (LEGACY_BUILDS[0][0], CURRENT_BUILD, False),
            (CURRENT_BUILD, 0xBD88B673, False),
            (CURRENT_BUILD, LEGACY_BUILDS[2][1], False),
            (LEGACY_BUILDS[1][0], CURRENT_BUILD, True),
        ):
            data = Data.from_buffer_copy(current)
            data.build = source
            self.reseal(data)
            if corrupt: data.checksum ^= 1
            before = bytes(data)
            self.assertFalse(self.lib.reidentify(C.byref(data), target))
            self.assertEqual(bytes(data), before)
    def test_unknown_or_wrong_region_identity_rejects_even_when_checksums_are_valid(self):
        _, live = self.capture()
        for build in (17, 0xBD88B673, LEGACY_BUILDS[0][0], LEGACY_BUILDS[2][1]):
            saved = Data.from_buffer_copy(live)
            saved.build = build
            self.reseal(saved)
            self.assertTrue(self.lib.valid(C.byref(saved)))
            self.assertFalse(self.lib.match(C.byref(saved), C.byref(live)))
            self.assertFalse(self.lib.match(C.byref(saved), C.byref(saved)))
        saved = Data.from_buffer_copy(live)
        saved.game = 1
        self.reseal(saved)
        self.assertFalse(self.lib.match(C.byref(saved), C.byref(live)))

    def test_every_owner_byte_including_unused_tail_must_still_match(self):
        _, live = self.capture()
        for change in (
            lambda p: setattr(p, 'config', p.config + 1),
            lambda p: setattr(p, 'count', p.count - 1),
            lambda p: setattr(p, 'keepCount', p.keepCount - 1),
            lambda p: setattr(p.anchors[0], 'value', p.anchors[0].value ^ 4),
            lambda p: setattr(p.anchors[p.count], 'value', 1),
            lambda p: setattr(p.keep[p.keepCount], 'address', 0x80000000),
            lambda p: setattr(p.keep[0], 'size', p.keep[0].size - 1),
        ):
            saved = Data.from_buffer_copy(live)
            saved.build = LEGACY_BUILDS[1][0]
            change(saved)
            self.reseal(saved)
            self.assertTrue(self.lib.valid(C.byref(saved)))
            self.assertFalse(self.lib.match(C.byref(saved), C.byref(live)))

    def test_valid_owner_graph_and_volatile_controller_thread_bytes_stay_compatible(self):
        ok,a=self.capture();self.assertTrue(ok,hex(self.lib.failure()))
        self.assertEqual(C.sizeof(Data),5920);self.assertTrue(self.lib.valid(C.byref(a)))
        pad=self.mem[self.layout.application+0x20]
        for address in (pad+0x68,pad+0x9c,pad+0xa0,self.layout.setupThread+0x10):
            self.mem[address]=123456
        # The captured game's allocator/disposer state rewinds independently.
        for address in (self.stage+0x58,self.stage+0x5c,self.stage+0x60,
                        self.stage+0x68,self.stage+0x6c,self.stage+0x74):
            self.mem[address]=123456
        ok,b=self.capture();self.assertTrue(ok)
        self.assertTrue(self.lib.match(C.byref(a),C.byref(b)))

    def test_changed_resource_backing_heap_layout_and_service_owner_refuse_match(self):
        ok,saved=self.capture();self.assertTrue(ok)
        for address in (self.volume+0x64,self.root+0x90+4,self.layout.application+0x1c):
            original=self.mem.get(address,0);self.mem[address]=original+4
            ok,live=self.capture()
            self.assertFalse(ok and self.lib.match(C.byref(saved),C.byref(live)))
            self.mem[address]=original

    def test_busy_mutex_unknown_archive_cycles_and_bad_backing_refuse_without_profile(self):
        for address,value in ((self.stage+0x20,0x80301100),(self.volume,self.layout.aramArchiveVtable),
                              (self.volume+0x24,self.volume+0x18),
                              (self.stage+0x2004,0x7fffffff)):
            old=self.mem.get(address,0);self.mem[address]=value
            ok,data=self.capture();self.assertFalse(ok);self.assertEqual(bytes(data),bytes(C.sizeof(data)))
            self.mem[address]=old

    def test_heap_tree_cycle_and_out_of_bounds_allocation_refuse(self):
        for address,value in ((self.stage+0x44,33),
                              (self.stage+0x3c,self.stage+0x48),
                              (self.root+0x90+4,0x7fffffff)):
            old=self.mem.get(address,0);self.mem[address]=value
            ok,data=self.capture();self.assertFalse(ok);self.assertEqual(data.magic,0)
            self.mem[address]=old

    def test_corrupt_profile_counts_and_keep_ranges_are_not_trusted(self):
        for field,value in (('count',641),('keepCount',97),('version',2),('build',18)):
            ok,data=self.capture();self.assertTrue(ok);setattr(data,field,value)
            self.assertFalse(self.lib.valid(C.byref(data)))

    def test_filter_preserves_partial_cross_fragment_runtime_ranges_exactly(self):
        k=C.windll.kernel32;k.VirtualAlloc.argtypes=[C.c_void_p,C.c_size_t,U,U];k.VirtualAlloc.restype=C.c_void_p
        k.VirtualFree.argtypes=[C.c_void_p,C.c_size_t,U]
        base=k.VirtualAlloc(0x81000000,0x10000,0x3000,4)
        if base!=0x81000000:
            if base:k.VirtualFree(base,0,0x8000)
            self.skipTest('Low test address unavailable')
        try:
            data=Data();data.keepCount=3
            for i,(off,n) in enumerate(((0,8),(17,11),(49,20))):data.keep[i]=Range(base+off,n)
            C.memset(base,0xa5,80);source=C.create_string_buffer(bytes(range(80)))
            for offset,n in ((0,5),(5,18),(23,19),(42,38)):
                self.lib.filtered(C.byref(data),base+offset,C.addressof(source)+offset,n)
            expected=bytearray(range(80))
            for off,n in ((0,8),(17,11),(49,20)):expected[off:off+n]=bytes([0xa5])*n
            self.assertEqual(C.string_at(base,80),bytes(expected))
        finally:k.VirtualFree(base,0,0x8000)

if __name__=='__main__':unittest.main()
