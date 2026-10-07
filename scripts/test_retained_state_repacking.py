"""Exercise real retained-state repacking, bank commits and candidate retries."""
import ctypes as C
from pathlib import Path
import random
import subprocess
import tempfile
import unittest
import zlib

from test_practice_tape import function_source
from test_state_codec import Guarded, Span, Result, reference_quick_frame
from test_state_pool_memory import Memory
from test_state_slot_pool import Pool

ROOT = Path(__file__).resolve().parents[1]


class RetainedStateRepackingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='moonshine-retained-repack-')
        cls.addClassCleanup(cls.temp.cleanup)
        functions = ''
        for name in ('u32 poolCapacity(', 'void poolWriteSpans(', 'void poolReadSpans(',
                     'u32 packedChecksum(', 'bool commitPackedState(', 'u32 metadataTag(',
                     'bool compressCandidate(', 'bool quickStoredState(',
                     'bool repackRetainedState(', 'bool repackForCandidate('):
            body = function_source(ROOT/'src/savestate.cpp', name)
            if name == 'bool repackRetainedState(':
                body = body.replace('    StoredState &saved', '    testStage=100+slot; ++repackCalls[slot][compact];\n    StoredState &saved')
                body = body.replace('    // A retained source', '    testStage=200+slot; lastRepack = result;\n    // A retained source')
            if name == 'bool compressCandidate(':
                body = body.replace('    StateCodec::WriteSpan output', '    testStage=10; ++candidateCalls;\n    StateCodec::WriteSpan output')
                body = body.replace('StateCodec::compress(', 'countedCompress(')
            body = body.replace('StateSlotPoolCommitBanked(', 'observedCommit(')
            functions += body + '\n'
        source = r'''
#include "susamune/state_codec.hxx"
#include "susamune/state_crc.hxx"
#include "susamune/state_pool_memory.h"
typedef unsigned int u32;typedef unsigned char u8;
extern "C" void *memcpy(void*d,const void*s,__SIZE_TYPE__ n){u8*a=(u8*)d;const u8*b=(const u8*)s;while(n--)*a++=*b++;return d;}
extern "C" void *memset(void*d,int c,__SIZE_TYPE__ n){u8*a=(u8*)d;while(n--)*a++=(u8)c;return d;}
extern "C" void *memmove(void*d,const void*s,__SIZE_TYPE__ n){u8*a=(u8*)d;const u8*b=(const u8*)s;if(a<b){for(__SIZE_TYPE__ i=0;i<n;++i)a[i]=b[i];}else{while(n){--n;a[n]=b[n];}}return d;}
extern "C" int memcmp(const void*a,const void*b,__SIZE_TYPE__ n){const u8*x=(const u8*)a,*y=(const u8*)b;while(n--){if(*x!=*y)return *x-*y;++x;++y;}return 0;}
struct SavestateManager {enum{kSlotCount=3};};
struct StoredState {u8 sidecars[512];u32 generation,rawSize,packedSize,adler32,parentEpisode,metadataTag;};
StoredState sSlots[3];StateSlotPool sPool;StatePoolMemory sPoolMemory;
u32 sPackedChecksums[3],sDurableSlots,testStagingBytes,repackCalls[3][2],candidateCalls,candidateModes[3];
StateCodec::Result lastRepack;volatile u32 testStage;
static bool observedCommit(StateSlotPool*p,const StatePoolMemory*m,u32 slot,u32 n,const u8*t,u32 size){
 testStage=300+slot;bool result=StateSlotPoolCommitBanked(p,m,slot,n,t,size);testStage=400+slot;return result;}
u8 *staging;void *work;
#define kStagingBase staging
#define SUSAMUNE_STATE_STAGING_SIZE testStagingBytes
#define SUSAMUNE_STATE_CODEC_WORKSPACE_SIZE 0x4e000u
void *codecWorkspace(){return work;}
StateCodec::Result countedCompress(void*w,u32 ws,const StateCodec::ReadSpan*s,u32 n,
 const StateCodec::WriteSpan*d,u32 dn,bool compact=false,bool quick=false){
 ++candidateModes[quick?0:compact?2:1];return StateCodec::compress(w,ws,s,n,d,dn,compact,quick);}
''' + functions + r'''
#define API extern "C" __declspec(dllexport)
API void setup(StatePoolMemory*m,u8*t,u32 ts,void*w){
 sPoolMemory=*m;staging=t;testStagingBytes=ts;work=w;memset(&sPool,0,sizeof(sPool));
 memset(sSlots,0,sizeof(sSlots));memset(repackCalls,0,sizeof(repackCalls));memset(candidateModes,0,sizeof(candidateModes));candidateCalls=0;sDurableSlots=7;
}
API u32 measure(const void*p,u32 n,u32 mode){StateCodec::ReadSpan source={p,n};
 return StateCodec::compress(work,0x4e000,&source,1,0,0,mode==2,mode==0).compressedBytes;}
API int install(u32 slot,const void*p,u32 n,u32 mode){
 StateCodec::ReadSpan source={p,n};StateCodec::WriteSpan out[2];poolWriteSpans(sPool.used,poolCapacity()-sPool.used,out);
 StateCodec::Result r=StateCodec::compress(work,0x4e000,&source,1,out,2,mode==2,mode==0);
 if(r.status!=StateCodec::SUCCESS)return 0;
 sPool.slots[slot]={sPool.used,r.compressedBytes};sPool.used+=r.compressedBytes;
 StoredState&s=sSlots[slot];memset(s.sidecars,41+slot,sizeof(s.sidecars));s.generation=100+slot;
 s.parentEpisode=5+slot;s.rawSize=n;s.packedSize=r.compressedBytes;s.adler32=r.adler32;s.metadataTag=metadataTag(s);
 sPackedChecksums[slot]=packedChecksum(sPool.slots[slot].offset,s.packedSize);return 1;
}
API int save(u32 slot,const void*p,u32 n){StateCodec::ReadSpan source={p,n};StateCodec::Result r;
 u32 sizes[3]={};bool fits=compressCandidate(&source,1,n,slot,r,sizes,2);if(!fits)fits=repackForCandidate(&source,1,n,slot,r,sizes);
 return fits;
}
API const StateSlotPool *pool(){return &sPool;}
API const void *meta(u32 slot){return &sSlots[slot];}
API u32 metaSize(){return sizeof(StoredState);}
API u32 checksum(u32 slot){return sPackedChecksums[slot];}
API u32 calls(u32 slot,u32 mode){return repackCalls[slot][mode];}
API u32 candidates(){return candidateCalls;}
API u32 modeCalls(u32 mode){return candidateModes[mode];}
API u32 stage(){return testStage;}
API u32 durable(){return sDurableSlots;}
API const StateCodec::Result *last(){return &lastRepack;}
API void corrupt(u32 slot,u32 correctCrc){StatePoolMemorySpan piece;
 StatePoolMemorySpanAt(&sPoolMemory,sPool.slots[slot].offset+12,1,&piece);*piece.data^=255;
 if(correctCrc)sPackedChecksums[slot]=packedChecksum(sPool.slots[slot].offset,sPool.slots[slot].size);}
'''
        path = Path(cls.temp.name)/'repack.cpp'; path.write_text(source)
        result = subprocess.run([str(ROOT/'toolchain/clang++.exe'), '--target=x86_64-pc-windows-msvc',
            '-shared','-nostdlib','-fuse-ld=lld','-Wl,/noentry','-O2','-fno-builtin',
            '-mno-stack-arg-probe','-I',str(ROOT/'include'),str(path),str(ROOT/'src/state_codec.cpp'),
            str(ROOT/'src/state_crc.cpp'),'-o',str(path.with_suffix('.dll'))],capture_output=True,text=True)
        if result.returncode: raise AssertionError(result.stdout+result.stderr)
        cls.lib=C.CDLL(str(path.with_suffix('.dll')))
        cls.addClassCleanup(lambda:C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))
        cls.lib.setup.argtypes=[C.POINTER(Memory),C.c_void_p,C.c_uint,C.c_void_p]
        cls.lib.measure.argtypes=[C.c_void_p,C.c_uint,C.c_uint]
        cls.lib.install.argtypes=[C.c_uint,C.c_void_p,C.c_uint,C.c_uint]
        cls.lib.save.argtypes=[C.c_uint,C.c_void_p,C.c_uint]
        cls.lib.pool.restype=C.POINTER(Pool);cls.lib.meta.restype=C.c_void_p
        cls.lib.last.restype=C.POINTER(Result)
        cls.lib.checksum.restype=C.c_uint

    def setup_pool(self, capacity, staging=0x100000):
        self.banks=[Guarded(capacity//2),Guarded(capacity-capacity//2)]
        self.work,self.staging=Guarded(0x4e000),Guarded(staging)
        self.memory=Memory((C.c_void_p*2)(*[b.ptr for b in self.banks]),(C.c_uint*2)(*[b.size for b in self.banks]))
        self.lib.setup(C.byref(self.memory),self.staging.ptr,staging,self.work.ptr)

    def packed(self, slot):
        pool=self.lib.pool().contents;entry=pool.slots[slot]
        whole=b''.join(C.string_at(b.ptr,b.size) for b in self.banks)
        return whole[entry.offset:entry.offset+entry.size]

    def metadata(self, slot):
        return C.string_at(self.lib.meta(slot),self.lib.metaSize())

    def check_retained(self, slot, raw, old_meta):
        packed=self.packed(slot)
        decoded=reference_quick_frame(packed,len(raw)) if packed.startswith(b'MSL4') else zlib.decompress(packed)
        self.assertEqual(decoded,raw)
        current=self.metadata(slot)
        self.assertEqual(current[:520],old_meta[:520])
        self.assertEqual(current[524:532],old_meta[524:532])
        self.assertEqual(self.lib.checksum(slot),zlib.crc32(packed))
        self.assertEqual(self.lib.durable(),7)

    def test_retained_quick_states_shrink_only_when_third_save_needs_room(self):
        raw=(random.Random(508).randbytes(8192)+b'A'*2048)*60
        self.setup_pool(2000000);owner=C.create_string_buffer(raw)
        quick,fast,compact=[self.lib.measure(owner,len(raw),mode) for mode in range(3)]
        self.assertGreater(quick,fast)
        capacity=2*quick+compact-1
        self.setup_pool(capacity)
        for slot in (0,1):self.assertEqual(self.lib.install(slot,owner,len(raw),0),1)
        before=[self.metadata(i) for i in (0,1)]
        self.assertEqual(self.lib.save(2,owner,len(raw)),1)
        for i in (0,1):self.check_retained(i,raw,before[i])
        self.assertEqual(self.lib.calls(0,0),1)
        self.assertEqual(self.lib.candidates(),2)
        # The first quick result still cannot fit after this retained repack.
        # Its measured size must skip that entire repeat, while fast is retried.
        self.assertEqual([self.lib.modeCalls(mode) for mode in range(3)], [1, 2, 0])
        self.assertTrue(self.work.guards() and self.staging.guards() and all(b.guards() for b in self.banks))

    def test_refusal_with_tiny_staging_preserves_all_retained_bytes(self):
        raw=random.Random(901).randbytes(200000);owner=C.create_string_buffer(raw)
        self.setup_pool(410000,0x4e020)
        for slot in (0,1):self.assertEqual(self.lib.install(slot,owner,len(raw),0),1)
        before=[(self.packed(i),self.metadata(i)) for i in (0,1)]
        self.assertEqual(self.lib.save(2,owner,len(raw)),0)
        for i in (0,1):self.assertEqual((self.packed(i),self.metadata(i)),before[i])
        self.assertEqual([self.lib.modeCalls(mode) for mode in range(3)], [1, 1, 1])
        self.assertEqual([[self.lib.calls(i,m) for m in range(2)] for i in (0,1)],[[1,1],[1,1]])

    def test_bad_retained_checksum_cannot_be_reencoded_or_republished(self):
        raw=random.Random(902).randbytes(200000);owner=C.create_string_buffer(raw)
        self.setup_pool(410000)
        for slot in (0,1):self.assertEqual(self.lib.install(slot,owner,len(raw),0),1)
        self.lib.corrupt(0,0);self.lib.corrupt(1,0)
        before=[(self.packed(i),self.metadata(i),self.lib.checksum(i)) for i in (0,1)]
        self.assertEqual(self.lib.save(2,owner,len(raw)),0)
        for i in (0,1):self.assertEqual((self.packed(i),self.metadata(i),self.lib.checksum(i)),before[i])

    def test_compact_second_pass_revisits_only_original_quick_slots(self):
        raw=random.Random(907).randbytes(200000)+(random.Random(508).randbytes(8192)+b'A'*2048)*60
        owner=C.create_string_buffer(raw);self.setup_pool(3000000)
        quick,fast,compact=[self.lib.measure(owner,len(raw),mode) for mode in range(3)]
        self.assertLess(2*quick,3*compact)
        self.assertGreater(fast,compact)
        self.setup_pool(3*compact)
        for slot in (0,1):self.assertEqual(self.lib.install(slot,owner,len(raw),0),1)
        before=[self.metadata(i) for i in (0,1)]
        self.assertEqual(self.lib.save(2,owner,len(raw)),1)
        for i in (0,1):
            self.check_retained(i,raw,before[i])
            self.assertEqual([self.lib.calls(i,m) for m in (0,1)],[1,1])
        self.assertEqual([self.lib.modeCalls(mode) for mode in range(3)], [1, 1, 2])

    def test_previously_fast_states_can_compact_for_a_later_save(self):
        raw=random.Random(907).randbytes(200000)+(random.Random(508).randbytes(8192)+b'A'*2048)*60
        owner=C.create_string_buffer(raw);self.setup_pool(3000000)
        fast,compact=[self.lib.measure(owner,len(raw),mode) for mode in (1,2)]
        self.assertLess(2*fast,3*compact)
        self.setup_pool(3*compact)
        for slot in (0,1):self.assertEqual(self.lib.install(slot,owner,len(raw),1),1)
        before=[self.metadata(i) for i in (0,1)]
        self.assertEqual(self.lib.save(2,owner,len(raw)),1)
        for i in (0,1):
            self.check_retained(i,raw,before[i])
            self.assertEqual([self.lib.calls(i,m) for m in (0,1)],[0,1])
        self.assertEqual([self.lib.modeCalls(mode) for mode in range(3)], [1, 1, 2])

    def test_malformed_retained_stream_with_correct_crc_preserves_all_slots(self):
        raw=random.Random(908).randbytes(200000);owner=C.create_string_buffer(raw)
        self.setup_pool(410000)
        for slot in (0,1):
            self.assertEqual(self.lib.install(slot,owner,len(raw),0),1)
            self.lib.corrupt(slot,1)
        before=[(self.packed(i),self.metadata(i),self.lib.checksum(i)) for i in (0,1)]
        self.assertEqual(self.lib.save(2,owner,len(raw)),0)
        for i in (0,1):self.assertEqual((self.packed(i),self.metadata(i),self.lib.checksum(i)),before[i])

    def test_final_refusal_preserves_selected_old_slot_and_shrunken_neighbors(self):
        raw=(random.Random(509).randbytes(8192)+b'A'*2048)*60
        owner=C.create_string_buffer(raw);self.setup_pool(2000000)
        quick=self.lib.measure(owner,len(raw),0);self.setup_pool(3*quick)
        for slot in range(3):self.assertEqual(self.lib.install(slot,owner,len(raw),0),1)
        before=[self.metadata(i) for i in range(3)];selected=self.packed(0)
        large=random.Random(909).randbytes(1000000);candidate=C.create_string_buffer(large)
        self.assertEqual(self.lib.save(0,candidate,len(large)),0)
        for i in range(3):self.check_retained(i,raw,before[i])
        self.assertEqual(self.packed(0),selected)
        self.assertEqual([self.lib.calls(0,m) for m in (0,1)],[0,0])
        self.assertLess(len(self.packed(1)),quick)
        self.assertLess(len(self.packed(2)),quick)
        self.assertEqual([self.lib.modeCalls(mode) for mode in range(3)], [1, 1, 1])

    def test_existing_deflate_states_skip_fast_pass_and_fitting_save_does_no_extra_work(self):
        raw=random.Random(903).randbytes(100000);owner=C.create_string_buffer(raw)
        for capacity,expected in ((210000,0),(400000,1)):
            self.setup_pool(capacity)
            for slot in (0,1):self.assertEqual(self.lib.install(slot,owner,len(raw),1),1)
            before=[(self.packed(i),self.metadata(i)) for i in (0,1)]
            self.assertEqual(self.lib.save(2,owner,len(raw)),expected)
            for i in (0,1):
                self.check_retained(i,raw,before[i][1])
                if expected:self.assertEqual(self.packed(i),before[i][0])
                else:self.assertLessEqual(len(self.packed(i)),len(before[i][0]))
                self.assertEqual([self.lib.calls(i,m) for m in (0,1)],[0,0 if expected else 1])


if __name__=='__main__':unittest.main()
