"""Use the production codec and save commit helper at temporary capacity limits."""

import ctypes as C
from pathlib import Path
import random
import subprocess
import tempfile
import unittest
import zlib

from test_native_timer_creation import function
from test_state_codec import Guarded, Span, Result, reference_quick_frame
from test_state_slot_pool import Pool, Entry, metadata
from test_state_pool_memory import Memory

ROOT = Path(__file__).resolve().parents[1]


class SavestateRecompressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / "toolchain/clang++.exe"
        if not compiler.exists():
            raise unittest.SkipTest("Bundled Windows compiler required")
        cls.temp = tempfile.TemporaryDirectory(prefix="moonshine-recompress-")
        cls.addClassCleanup(cls.temp.cleanup)
        source = Path(cls.temp.name) / "commit.cpp"
        production = (ROOT / "src/savestate.cpp").read_text()
        helper = function(production, "commitPackedState")
        # A trap remains fatal in production; record it here without crashing
        # the host so the test can inspect that no slot was published afterward.
        helper = helper.replace("__builtin_trap()", "return trapped()")
        helper = helper.replace("StateCodec::compress(", "testCompress(")
        candidate = function(production, "compressCandidate")
        source.write_text(r'''
#include "susamune/state_codec.hxx"
#include "susamune/state_slot_pool.h"
#include "susamune/state_pool_memory.h"
typedef unsigned int u32;typedef unsigned char u8;
extern "C" void *memcpy(void*d,const void*s,__SIZE_TYPE__ n){u8*a=(u8*)d;const u8*b=(const u8*)s;while(n--)*a++=*b++;return d;}
extern "C" void *memset(void*d,int c,__SIZE_TYPE__ n){u8*a=(u8*)d;while(n--)*a++=(u8)c;return d;}
extern "C" void *memmove(void*d,const void*s,__SIZE_TYPE__ n){u8*a=(u8*)d;const u8*b=(const u8*)s;if(a<b){for(__SIZE_TYPE__ i=0;i<n;++i)a[i]=b[i];}else{while(n){--n;a[n]=b[n];}}return d;}
extern "C" int memcmp(const void*a,const void*b,__SIZE_TYPE__ n){const u8*x=(const u8*)a,*y=(const u8*)b;while(n--){if(*x!=*y)return *x-*y;++x;++y;}return 0;}
u32 capacity,stagingSize;
#define SUSAMUNE_STATE_POOL_SIZE capacity
#define SUSAMUNE_STATE_STAGING_SIZE stagingSize
#define SUSAMUNE_STATE_CODEC_WORKSPACE_SIZE 0x50000u
StateSlotPool sPool;StatePoolMemory sPoolMemory;u8 *staging;void *work;
#define kStagingBase staging
void *codecWorkspace(){return work;}
int fault,trapCount,secondCalls,lastCompact,lastQuick;
bool trapped(){++trapCount;return false;}
StateCodec::Result testCompress(void*w,u32 n,const StateCodec::ReadSpan*s,u32 count,const StateCodec::WriteSpan*d,u32 dn,bool compact,bool quick){
 ++secondCalls;lastCompact=compact;lastQuick=quick;StateCodec::Result r=StateCodec::compress(w,n,s,count,d,dn,compact,quick);
 if(fault==1)r.status=StateCodec::CODEC_ERROR;
 if(fault==2)++r.rawBytes;
 if(fault==3)++r.compressedBytes;
 if(fault==4)r.adler32^=1;
 return r;
}
''' + function(production, "poolCapacity") + function(production, "poolWriteSpans") + helper + candidate + r'''
extern "C" __declspec(dllexport) int run(StateSlotPool*p,StatePoolMemory*m,u32 cap,u8*t,u32 ts,
 void*w,const StateCodec::ReadSpan*s,u32 count,u32 raw,u32 slot,int bad,u32 compact,u32 quick,StateCodec::Result*out,int*calls){
 sPool=*p;sPoolMemory=*m;capacity=cap;staging=t;stagingSize=ts;work=w;fault=bad;trapCount=secondCalls=0;lastCompact=lastQuick=-1;
 StateCodec::WriteSpan destination[3]={{t,ts},{0,0},{0,0}};
 poolWriteSpans(p->used,cap-p->used,destination+1);
 *out=StateCodec::compress(w,SUSAMUNE_STATE_CODEC_WORKSPACE_SIZE,s,count,destination,3,compact!=0,quick!=0);
 bool ok=commitPackedState(s,count,raw,slot,*out,compact!=0,quick!=0);
 *p=sPool;*calls=secondCalls;return trapCount?-1:ok;
}
extern "C" __declspec(dllexport) u32 compactMode(){return lastCompact;}
extern "C" __declspec(dllexport) u32 quickMode(){return lastQuick;}
extern "C" __declspec(dllexport) u32 measure(void*w,const StateCodec::ReadSpan*s,u32 count,u32 compact,u32 quick){
 return StateCodec::compress(w,SUSAMUNE_STATE_CODEC_WORKSPACE_SIZE,s,count,0,2,compact!=0,quick!=0).compressedBytes;
}
extern "C" __declspec(dllexport) int runAdaptive(StateSlotPool*p,StatePoolMemory*m,u8*t,u32 ts,
 void*w,const StateCodec::ReadSpan*source,u32 count,u32 rawSize,u32 slot,StateCodec::Result*out){
 sPool=*p;sPoolMemory=*m;capacity=StatePoolMemoryCapacity(m);staging=t;stagingSize=ts;work=w;fault=0;trapCount=secondCalls=0;lastCompact=lastQuick=-1;
 StateCodec::WriteSpan output[3]={{t,ts},{0,0},{0,0}};poolWriteSpans(p->used,capacity-p->used,output+1);
 StateCodec::Result result;
 u32 sizes[3]={};const bool fits=compressCandidate(source,count,rawSize,slot,result,sizes);
 *p=sPool;*out=result;return trapCount?-1:fits;
}
''', encoding="ascii")
        dll = source.with_suffix(".dll")
        subprocess.run([str(compiler), "--target=x86_64-pc-windows-msvc", "-shared",
                        "-nostdlib", "-fuse-ld=lld", "-Wl,/noentry", "-O2",
                        "-fno-builtin", "-mno-stack-arg-probe", "-I", str(ROOT / "include"),
                        str(source), str(ROOT / "src/state_codec.cpp"), "-o", str(dll)], check=True)
        cls.lib = C.CDLL(str(dll))
        from _ctypes import FreeLibrary
        cls.addClassCleanup(FreeLibrary, cls.lib._handle)
        cls.lib.run.argtypes = [C.POINTER(Pool), C.POINTER(Memory), C.c_uint, C.c_void_p,
            C.c_uint, C.c_void_p, C.POINTER(Span), C.c_uint, C.c_uint, C.c_uint,
            C.c_int, C.c_uint, C.c_uint, C.POINTER(Result), C.POINTER(C.c_int)]
        cls.lib.measure.argtypes=[C.c_void_p,C.POINTER(Span),C.c_uint,C.c_uint,C.c_uint]
        cls.lib.measure.restype=C.c_uint
        cls.lib.runAdaptive.argtypes=[C.POINTER(Pool),C.POINTER(Memory),C.c_void_p,C.c_uint,
            C.c_void_p,C.POINTER(Span),C.c_uint,C.c_uint,C.c_uint,C.POINTER(Result)]

    def execute(self, size, slot=1, fault=0, raw_offset=0, compact=False, quick=False):
        capacity = 220000
        pool = Pool((Entry * 3)(Entry(0, 70000), Entry(70000, 70000), Entry(140000, 70000)), 210000)
        buffers = [Guarded(150000), Guarded(70000)]
        memory = Memory((C.c_void_p * 2)(*[b.ptr for b in buffers]), (C.c_uint * 2)(150000, 70000))
        staging, work = Guarded(4096), Guarded(0x50000)
        originals = [bytes([31 + i * 29]) * 70000 for i in range(3)]
        initial = b"".join(originals) + bytes(10000)
        C.memmove(buffers[0].ptr, initial[:150000], 150000)
        C.memmove(buffers[1].ptr, initial[150000:], 70000)
        def read(offset, size):
            return b"".join(b.data() for b in buffers)[offset:offset + size]
        old = metadata(pool), read(0, pool.used)
        data = random.Random(173).randbytes(size)
        owner = C.create_string_buffer(data)
        source = (Span * 2)(Span(C.addressof(owner), size // 2),
                            Span(C.addressof(owner) + size // 2, size - size // 2))
        result, calls = Result(), C.c_int()
        status = self.lib.run(C.byref(pool), C.byref(memory), capacity, staging.ptr, staging.size,
                             work.ptr, source, 2, len(data) + raw_offset, slot, fault, compact, quick,
                             C.byref(result), C.byref(calls))
        self.assertTrue(all(b.guards() for b in buffers) and staging.guards() and work.guards())
        self.assertEqual(result.raw, len(data))
        self.assertEqual(result.adler, zlib.adler32(data))
        if calls.value:
            self.assertEqual(self.lib.compactMode(),int(compact))
            self.assertEqual(self.lib.quickMode(),int(quick))
        if status == 1:
            selected = pool.slots[slot]
            self.assertEqual(selected.size, result.compressed)
            encoded = read(selected.offset, selected.size)
            self.assertEqual(reference_quick_frame(encoded, len(data)) if quick else
                             zlib.decompress(encoded), data)
        else:
            if status == 0:
                self.assertEqual((metadata(pool), read(0, pool.used)), old)
            else:
                self.assertEqual(pool.slots[slot].size, 0)
        for i, entry in enumerate(pool.slots):
            if i != slot:
                self.assertEqual(read(entry.offset, entry.size), originals[i])
        return status, calls.value, result.status

    def test_full_temporary_buffer_recompresses_into_replaced_slot_for_each_slot(self):
        for slot in range(3):
            self.assertEqual(self.execute(65000, slot), (1, 1, 3))

    def test_staged_success_uses_one_pass(self):
        self.assertEqual(self.execute(1000), (1, 0, 0))

    def test_three_fresh_states_roundtrip_and_full_third_save_preserves_first_two(self):
        payloads = [random.Random(612 + slot).randbytes(70000) for slot in range(3)]
        for capacity, expected_last in ((220000, 1), (180000, 0)):
            with self.subTest(capacity=capacity):
                pool = Pool()
                buffers = [Guarded(145000), Guarded(capacity - 145000)]
                memory = Memory((C.c_void_p * 2)(*[b.ptr for b in buffers]),
                                (C.c_uint * 2)(*[b.size for b in buffers]))
                staging, work = Guarded(4096), Guarded(0x50000)
                published = []
                for slot, data in enumerate(payloads):
                    owner = C.create_string_buffer(data)
                    source = (Span * 1)(Span(C.addressof(owner), len(data)))
                    before = metadata(pool), b''.join(b.data() for b in buffers)[:pool.used]
                    result = Result()
                    success = self.lib.runAdaptive(C.byref(pool), C.byref(memory), staging.ptr,
                        staging.size, work.ptr, source, 1, len(data), slot, C.byref(result))
                    self.assertEqual(success, expected_last if slot == 2 else 1)
                    contents = b''.join(b.data() for b in buffers)
                    if success:
                        entry = pool.slots[slot]
                        encoded = contents[entry.offset:entry.offset + entry.size]
                        self.assertEqual(reference_quick_frame(encoded, len(data)), data)
                        published.append(encoded)
                    else:
                        self.assertEqual((metadata(pool), contents[:pool.used]), before)
                        self.assertEqual(pool.slots[slot].size, 0)
                    for retained, encoded in enumerate(published):
                        entry = pool.slots[retained]
                        self.assertEqual(contents[entry.offset:entry.offset + entry.size], encoded)
                    self.assertTrue(all(b.guards() for b in buffers) and work.guards() and staging.guards())

    def test_true_capacity_or_incomplete_count_failure_keeps_all_original_states(self):
        self.assertEqual(self.execute(90000), (0, 0, 3))
        self.assertEqual(self.execute(65000, raw_offset=1), (0, 0, 3))

    def test_second_pass_status_size_raw_and_checksum_mismatch_trap_without_publication(self):
        for fault in range(1, 5):
            self.assertEqual(self.execute(65000, fault=fault), (-1, 1, 3))

    def test_compact_recompression_preserves_mode_through_selected_slot_reclaim(self):
        for slot in range(3):
            self.assertEqual(self.execute(65000,slot,compact=True),(1,1,3))
        self.assertEqual(self.execute(1000,compact=True),(1,0,0))

    def test_quick_recompression_keeps_mode_and_other_slots_in_both_banks(self):
        for slot in range(3):
            self.assertEqual(self.execute(65000, slot, quick=True), (1, 1, 3))
        self.assertEqual(self.execute(1000, quick=True), (1, 0, 0))
        self.assertEqual(self.execute(90000, quick=True), (0, 0, 3))
        self.assertEqual(self.execute(65000, raw_offset=1, quick=True), (0, 0, 3))
        for fault in range(1, 5):
            self.assertEqual(self.execute(65000, fault=fault, quick=True), (-1, 1, 3))

    def test_adaptive_save_chooses_quick_then_deflate_then_compact_by_actual_capacity(self):
        data = (random.Random(508).randbytes(8192) + b'A' * 2048) * 60
        work, staging = Guarded(0x50000), Guarded(32)
        owner = C.create_string_buffer(data)
        source = (Span * 3)(Span(C.addressof(owner), 19),
                           Span(C.addressof(owner) + 19, 0x20000 - 13),
                           Span(C.addressof(owner) + 0x20000 + 6, len(data) - 0x20000 - 6))
        quick = self.lib.measure(work.ptr, source, 3, False, True)
        fast = self.lib.measure(work.ptr, source, 3, False, False)
        compact = self.lib.measure(work.ptr, source, 3, True, False)
        self.assertGreater(quick, fast + 2)
        self.assertGreater(fast, compact + 2)
        for space, expected, quick_mode, compact_mode in (
                (quick, quick, 1, 0),
                ((quick + fast) // 2, fast, 0, 0),
                ((fast + compact) // 2, compact, 0, 1)):
            with self.subTest(space=space, expected=expected):
                capacity = space + 128
                pool = Pool((Entry * 3)(Entry(0, 64), Entry(64, space), Entry(64 + space, 64)), capacity)
                buffers = [Guarded(capacity // 2), Guarded(capacity - capacity // 2)]
                memory = Memory((C.c_void_p * 2)(*[b.ptr for b in buffers]),
                                (C.c_uint * 2)(*[b.size for b in buffers]))
                initial = b'a' * 64 + b'b' * space + b'c' * 64
                C.memmove(buffers[0].ptr, initial[:buffers[0].size], buffers[0].size)
                C.memmove(buffers[1].ptr, initial[buffers[0].size:], buffers[1].size)
                result = Result()
                self.assertEqual(self.lib.runAdaptive(C.byref(pool), C.byref(memory), staging.ptr,
                    staging.size, work.ptr, source, len(source), len(data), 1, C.byref(result)), 1)
                self.assertEqual((self.lib.quickMode(), self.lib.compactMode(), result.compressed),
                                 (quick_mode, compact_mode, expected))
                copied = b''.join(b.data() for b in buffers)
                for index, value in ((0, b'a' * 64), (2, b'c' * 64)):
                    entry = pool.slots[index]
                    self.assertEqual(copied[entry.offset:entry.offset + entry.size], value)
                entry = pool.slots[1]
                encoded = copied[entry.offset:entry.offset + entry.size]
                self.assertEqual(reference_quick_frame(encoded, len(data)) if quick_mode else
                                 zlib.decompress(encoded), data)
                self.assertTrue(all(b.guards() for b in buffers) and work.guards() and staging.guards())

    def test_actual_save_falls_back_to_compact_when_only_compact_fits(self):
        data=(random.Random(508).randbytes(8192)+b'A'*2048)*60
        work,staging=Guarded(0x50000),Guarded(32)
        owner=C.create_string_buffer(data);source=(Span*1)(Span(C.addressof(owner),len(data)))
        fast=self.lib.measure(work.ptr,source,1,False,False)
        compact=self.lib.measure(work.ptr,source,1,True,False)
        self.assertGreater(fast,compact+2)
        selected=(fast+compact)//2;capacity=selected+128
        pool=Pool((Entry*3)(Entry(0,64),Entry(64,selected),Entry(64+selected,64)),capacity)
        buffers=[Guarded(capacity//2),Guarded(capacity-capacity//2)]
        memory=Memory((C.c_void_p*2)(*[b.ptr for b in buffers]),(C.c_uint*2)(*[b.size for b in buffers]))
        initial=b'a'*64+b'b'*selected+b'c'*64
        C.memmove(buffers[0].ptr,initial[:buffers[0].size],buffers[0].size)
        C.memmove(buffers[1].ptr,initial[buffers[0].size:],buffers[1].size)
        result=Result()
        self.assertEqual(self.lib.runAdaptive(C.byref(pool),C.byref(memory),staging.ptr,staging.size,
                         work.ptr,source,1,len(data),1,C.byref(result)),1)
        self.assertEqual((self.lib.compactMode(),result.compressed),(1,compact))
        copied=b''.join(b.data() for b in buffers)
        for index,expected in ((0,b'a'*64),(2,b'c'*64)):
            entry=pool.slots[index];self.assertEqual(copied[entry.offset:entry.offset+entry.size],expected)
        entry=pool.slots[1]
        self.assertEqual(zlib.decompress(copied[entry.offset:entry.offset+entry.size]),data)
        self.assertTrue(all(b.guards() for b in buffers) and work.guards() and staging.guards())


if __name__ == "__main__":
    unittest.main()
