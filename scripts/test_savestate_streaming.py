"""Production SD-window reader, recovery admission and restore dispatch under faults."""
import ctypes as C
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import unittest
import zlib

from test_practice_tape import function_source
from test_state_codec import Span, QUICK_BLOCK

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/savestate.cpp'

FIXTURE = r'''
#include "susamune/state_storage.h"
#include "susamune/state_slot_pool.h"
#include "susamune/state_pool_memory.h"
#include "susamune/state_codec.hxx"
#include "susamune/state_crc.hxx"
#include "susamune/state_live_video.hxx"
#include "susamune/state_restore_bindings.hxx"
static StateRestoreBindings::Words sRestoreBindings = {};
typedef unsigned int u32;typedef unsigned char u8;typedef long long OSTime;
extern "C" void *memcpy(void*d,const void*s,__SIZE_TYPE__ n){u8*a=(u8*)d;const u8*b=(const u8*)s;while(n--)*a++=*b++;return d;}
extern "C" void *memset(void*d,int c,__SIZE_TYPE__ n){u8*a=(u8*)d;while(n--)*a++=(u8)c;return d;}
extern "C" void *memmove(void*d,const void*s,__SIZE_TYPE__ n){u8*a=(u8*)d;const u8*b=(const u8*)s;
 if(a<b){for(__SIZE_TYPE__ i=0;i<n;++i)a[i]=b[i];}else{while(n){--n;a[n]=b[n];}}return d;}
extern "C" int memcmp(const void*a,const void*b,__SIZE_TYPE__ n){const u8*x=(const u8*)a,*y=(const u8*)b;
 while(n--){if(*x!=*y)return *x-*y;++x;++y;}return 0;}
#undef SUSAMUNE_STATE_STAGING_SIZE
#define SUSAMUNE_STATE_STAGING_SIZE 4096u
#define SUSAMUNE_GAME_VERSION 2u
#define OSSecondsToTicks(n) ((n)*1000ll)
static const u32 kSnapshotVersion=16,kMaxRegions=1;
static const u32 kStreamWindows=32;
static u8 bank0[250064],bank1[250064],stage[4160],live[300064];
alignas(32)static u8 workspace[0x4E040];
static StatePoolMemory sPoolMemory;
static StateSlotPool sPool;
static __UINTPTR_TYPE__ kStagingBase=(__UINTPTR_TYPE__)(stage+32);
struct Region{__UINTPTR_TYPE__ addr;u32 size,buf_offset;};
struct SavestateHeader{u32 version,game_version,heap_addr,heap_size,area_id,episode_id,region_count;Region regions[1];};
struct StoredState{SavestateHeader header;u32 ghost,practice,archiveProfile,generation,rawSize,packedSize,adler32,parentEpisode,metadataTag;};
static StoredState sSlots[3],sCandidate;
static SusamuneStateArchiveHeader sStreamHeader;
static u32 sPackedChecksums[3],sLoadSlot,sStreamSeen,sStreamOffset,sStreamSize,sStreamCrc,sStreamId;
static u32 sStreamChecksums[32],sLiveArchiveProfile,sDiskPoolUsed,sDurableSlots;
static bool sStreamCommit,sDiskStream,sDiskRecovered;
static StateLiveVideo::Range sLiveVideo = {};
static u32 rawSize,reads,writes,cancelled,errorText,ownerFault,regionFault,transportFault,faultOffset;
static const u8 *archiveBytes;static u32 archiveSize;
static u32 clockValue;
struct App{struct{u32 mAreaID,mEpisodeID;}mCurrentScene;}gpApplication;
class SavestateManager{public:enum{kSlotCount=3};};
static u32 parentEpisode(){return 3;}
static u32 gameBytes(){return rawSize-512;}
static bool validSnapshotRegions(const SavestateHeader*h,u32,u32){return !regionFault&&h->region_count==1&&h->regions[0].addr==(__UINTPTR_TYPE__)(live+32)&&h->regions[0].size==gameBytes();}
static void*codecWorkspace(){return workspace+32;}
static OSTime OSGetTime(){clockValue+=100;return clockValue;}
static void unmuteAudioDma(bool){}static void OSRestoreInterrupts(bool){}static bool sBusy;
static void feedback(const char*,const char*){++errorText;}
namespace Ghost{enum{kSavestateSpanCount=0};static bool savestateRestoreSpans(u32,StateCodec::WriteSpan*){return true;}}
namespace PracticeSession{enum{kSavestateSpanCount=2};static bool ownReplay;
static bool savestateRestoreSpans(u32 invalid,StateCodec::WriteSpan*out){out[0]={live+32+gameBytes(),236};out[1]={live+32+gameBytes()+236,276};return !invalid;}
static bool copySavestateBytes(void*d,const void*,u32 n){return ownReplay&&(u8*)d>=live+32+gameBytes()+236&&(u8*)d+n<=live+32+rawSize;}}
namespace StateArchiveProfile{
static bool matches(u32 saved,u32 current){return saved==current&&saved==71;}
static void copyGameBytes(void*,void*d,const void*s,u32 n){++writes;memcpy(d,s,n);}
}
static void captureArchiveProfile(u32&out){out=ownerFault?72:71;}
namespace StateStorage{
struct Result{u32 command,status,id;SusamuneStateArchiveHeader header;const void*metadata;char name[32];SusamuneStateWindowReceipt window;SusamuneTasManifest project;};
static Result result;static bool ready,pending;
static bool startWindow(u32 id,u32 crc,u32 packed,u32 offset,u32 size){
 ++reads;if(pending||id!=sStreamId||crc!=sStreamHeader.headerCrc||packed!=archiveSize||size>4096||offset+size>packed)return false;
 pending=ready=true;result={};result.command=SUSAMUNE_STATE_CMD_READ_WINDOW;
 result.id=id;result.header=sStreamHeader;result.metadata=&sCandidate;
 result.window.offset=offset;result.window.size=size;
 if(sStreamCommit&&offset>=faultOffset&&transportFault==1){result.status=SUSAMUNE_STATE_IO_ERROR;return true;}
 if(!sStreamCommit&&offset>=faultOffset&&transportFault==2){result.status=SUSAMUNE_STATE_CANCELLED;return true;}
 if(sStreamCommit&&offset>=faultOffset&&transportFault==5){ready=false;return true;}
 memcpy(stage+32,archiveBytes+offset,size);
 if(sStreamCommit&&offset>=faultOffset&&transportFault==3)stage[32]^=1;
 if(sStreamCommit&&offset>=faultOffset&&transportFault==4)++result.header.headerCrc;
 result.window.checksum=SusamuneStateCrc(stage+32,size);return true;
}
static void update(){}
static bool takeResult(Result&out){if(!ready)return false;out=result;ready=pending=false;return true;}
static bool cancel(){++cancelled;return true;}
}
'''

EXPORTS = r'''
extern "C" {
__declspec(dllexport) void reset(const StateCodec::ReadSpan*slots,const u32*adlers,u32 raw,
 const void*packed,u32 size,u32 adler){
 rawSize=raw;archiveBytes=(const u8*)packed;archiveSize=size;
 memset(bank0,0xC7,sizeof(bank0));memset(bank1,0xC7,sizeof(bank1));memset(stage,0xA5,sizeof(stage));
 memset(live,0xE3,sizeof(live));memset(workspace,0xB9,sizeof(workspace));memset(sSlots,0,sizeof(sSlots));
 sPool.used=slots[0].size+slots[1].size+slots[2].size;
 const u32 first=sPool.used/2;sPoolMemory={{bank0+32,bank1+32},{first,sPool.used-first}};
 u32 offset=0;
 for(u32 i=0;i<3;++i){sPool.slots[i]={offset,slots[i].size};StatePoolMemoryCopyIn(&sPoolMemory,offset,slots[i].data,slots[i].size);
  StoredState&s=sSlots[i];s.header={16,2,0x80500000,gameBytes(),1,2,1,{{(__UINTPTR_TYPE__)(live+32),gameBytes(),0}}};
  s.archiveProfile=71;s.generation=100+i;s.rawSize=raw;s.packedSize=slots[i].size;s.adler32=adlers[i];s.parentEpisode=3;
  sPackedChecksums[i]=packedChecksum(offset,slots[i].size);offset+=slots[i].size;}
 sCandidate=sSlots[0];sCandidate.generation=900;sCandidate.packedSize=size;sCandidate.adler32=adler;
 sStreamHeader={};sStreamHeader.headerCrc=187;sStreamHeader.payloadCrc=SusamuneStateCrc(packed,size);
 sStreamHeader.packedSize=size;sStreamHeader.metadataSize=sizeof(sCandidate);sStreamHeader.rawSize=raw;
 sStreamId=77;sLoadSlot=1;sLiveArchiveProfile=71;sStreamCommit=sDiskRecovered=false;sDiskStream=true;
 sStreamSeen=0;sStreamSize=0;sStreamCrc=0xffffffff;sStreamOffset=0;sDiskPoolUsed=sPool.used;
 StateStorage::pending=StateStorage::ready=false;
 gpApplication.mCurrentScene={1,2};reads=writes=cancelled=errorText=ownerFault=regionFault=transportFault=clockValue=0;
 faultOffset=8192;
 PracticeSession::ownReplay=false;sLiveVideo={0,0};
}
__declspec(dllexport) void replay(){PracticeSession::ownReplay=true;}
__declspec(dllexport) void protectVideo(u32 offset,u32 size){
 sLiveVideo={(__UINTPTR_TYPE__)(live+32+offset),(__UINTPTR_TYPE__)(live+32+offset+size)};}
__declspec(dllexport) void fault(u32 kind,u32 offset){transportFault=kind;faultOffset=offset;}
__declspec(dllexport) void change(u32 kind){
 if(kind==1)for(u32 i=0;i<3;++i)++sSlots[i].archiveProfile;
 if(kind==2)for(u32 i=0;i<3;++i){u8 b;StatePoolMemoryCopyOut(&sPoolMemory,sPool.slots[i].offset,&b,1);b^=1;StatePoolMemoryCopyIn(&sPoolMemory,sPool.slots[i].offset,&b,1);}
 if(kind==3)ownerFault=1;if(kind==4)regionFault=1;if(kind==5)++sStreamHeader.payloadCrc;
 if(kind==6)for(u32 i=0;i<3;++i)++sSlots[i].header.episode_id;
 if(kind==7)for(u32 i=0;i<3;++i)++sSlots[i].practice;
}
__declspec(dllexport) const void*output(){return live+32;}
__declspec(dllexport) void slotBytes(u32 slot,void*out){StatePoolMemoryCopyOut(&sPoolMemory,sPool.slots[slot].offset,out,sPool.slots[slot].size);}
__declspec(dllexport) u32 value(u32 key){switch(key){case 0:return sDiskRecovered;case 1:return writes;case 2:return reads;
case 3:return cancelled;case 4:return errorText;case 5:return sCandidate.generation;case 6:return sPool.used;}return 0;}
__declspec(dllexport) u32 generation(u32 slot){return sSlots[slot].generation;}
__declspec(dllexport) bool guards(){
 for(u32 i=0;i<32;++i)if(bank0[i]!=0xC7||bank1[i]!=0xC7||stage[i]!=0xA5||stage[4128+i]!=0xA5||
 live[i]!=0xE3||live[32+rawSize+i]!=0xE3||workspace[i]!=0xB9||workspace[32+0x4E000+i]!=0xB9)return false;return true;}
}
'''


class SavestateStreamingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / 'toolchain/clang++.exe'
        if not compiler.exists():raise unittest.SkipTest('Bundled host compiler required')
        cls.temp = tempfile.TemporaryDirectory(prefix='moonshine-stream-recovery-')
        cls.addClassCleanup(cls.temp.cleanup)
        text = FIXTURE
        for name in ('void poolWriteSpans(', 'void poolReadSpans(', 'u32 packedChecksum(',
                     'bool waitStateWindow(', 'bool readStateWindow(',
                     'void copyBaseStateBytes(', 'void copyOwnedStateBytes(', 'void copyStateBytes('):
            text += function_source(SOURCE, name)
        production = SOURCE.read_text()
        text += production[production.index('struct SDRecovery {'):production.index('bool prepareSDRecovery(')]
        text += function_source(SOURCE, 'bool prepareSDRecovery(')
        load = function_source(SOURCE, 'bool SavestateManager::loadSlot(')
        dispatch = load[load.index('    StateCodec::Status restored;'):load.rindex('    if (restored == StateCodec::COMMIT_FAILED)')]
        text += EXPORTS + r'''
extern "C" __declspec(dllexport) u32 restore(){
 bool fromSD=true,ints=true,dma=true,durable=true;u32 slot=3,heapStart=0x80500000,heapEnd=heapStart+gameBytes();
 StoredState&saved=sCandidate;SavestateHeader*h=&saved.header;
 StateCodec::ReadSpan compressed[3]={};StateCodec::WriteSpan destinations[3]={{live+32,gameBytes()},{live+32+gameBytes(),236},{live+32+gameBytes()+236,276}};
''' + dispatch + '\nreturn restored;\n}\n'
        path = Path(cls.temp.name) / 'test.cpp'
        path.write_text(text)
        proc = subprocess.run([str(compiler),'--target=x86_64-pc-windows-msvc','-shared','-O2',
            '-fno-builtin','-mno-stack-arg-probe','-nostdlib','-fuse-ld=lld','-Wl,/noentry',
            '-I',str(ROOT/'include'),str(path),str(ROOT/'src/state_codec.cpp'),str(ROOT/'src/state_crc.cpp'),
            '-o',str(path.with_suffix('.dll'))],capture_output=True,text=True)
        if proc.returncode:raise RuntimeError(proc.stdout + proc.stderr)
        cls.lib = C.CDLL(str(path.with_suffix('.dll')))
        cls.addClassCleanup(lambda:C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))
        cls.lib.reset.argtypes = [C.POINTER(Span),C.POINTER(C.c_uint),C.c_uint,C.c_void_p,C.c_uint,C.c_uint]
        cls.lib.output.restype = C.c_void_p
        cls.lib.slotBytes.argtypes = [C.c_uint,C.c_void_p]

    def setup_state(self, quick=False, bad=False):
        self.old_raw = [random.Random(seed).randbytes(100000) for seed in (1,2,3)]
        self.old_packed = [zlib.compress(raw) for raw in self.old_raw]
        self.owners = [C.create_string_buffer(data) for data in self.old_packed]
        slots = (Span*3)(*[Span(C.addressof(owner),len(data)) for owner,data in zip(self.owners,self.old_packed)])
        adlers = (C.c_uint*3)(*[zlib.adler32(data) for data in self.old_raw])
        self.raw = random.Random(77).randbytes(100000)
        packed = (b'MSL4'+struct.pack('>III',QUICK_BLOCK,len(self.raw),len(self.raw)|0x80000000)+self.raw
                  if quick else zlib.compress(self.raw))
        if bad:packed = packed[:-1]
        self.archive = C.create_string_buffer(packed)
        self.lib.reset(slots,adlers,len(self.raw),self.archive,len(packed),zlib.adler32(self.raw))
        self.capacity = self.lib.value(6)

    def output(self):return C.string_at(self.lib.output(),len(self.raw))

    def preserved(self):
        for slot,packed in enumerate(self.old_packed):
            output = C.create_string_buffer(len(packed))
            self.lib.slotBytes(slot,output)
            self.assertEqual(output.raw,packed)
            self.assertEqual(self.lib.generation(slot),100+slot)
        self.assertEqual(self.lib.value(6),self.capacity)
        self.assertTrue(self.lib.guards())

    def test_full_three_slot_pool_streams_both_formats_without_changing_any_slot(self):
        for quick in (False,True):
            with self.subTest(quick=quick):
                self.setup_state(quick)
                self.assertEqual(self.lib.restore(),0)
                self.assertEqual(self.output(),self.raw)
                self.assertEqual(self.lib.value(0),0)
                self.assertGreater(self.lib.value(2),30)
                self.preserved()

    def test_preflight_cancellation_bad_stream_and_owner_change_leave_live_state_unchanged(self):
        for quick in (False,True):
            for fault in ('cancel','stream','owners','crc','scene','regions','practice'):
                with self.subTest(quick=quick,fault=fault):
                    self.setup_state(quick,bad=fault=='stream')
                    if fault=='cancel':self.lib.fault(2,8192)
                    elif fault=='owners':self.lib.change(3)
                    elif fault=='crc':self.lib.change(5)
                    elif fault=='scene':self.lib.change(6)
                    elif fault=='regions':self.lib.change(4)
                    elif fault=='practice':self.lib.change(7)
                    self.lib.restore()
                    self.assertEqual(self.output(),b'\xe3'*len(self.raw))
                    self.assertEqual(self.lib.value(1),0)
                    self.assertEqual(self.lib.value(0),0)
                    self.preserved()

    def test_second_pass_io_mutation_identity_and_timeout_restore_prevalidated_ram_slot(self):
        for quick in (False,True):
            for fault in (1,3,4,5):
                with self.subTest(quick=quick,fault=fault):
                    self.setup_state(quick)
                    self.lib.fault(fault,65536)
                    self.assertEqual(self.lib.restore(),0)
                    self.assertEqual(self.output(),self.old_raw[1])
                    self.assertEqual(self.lib.value(0),1)
                    self.assertEqual(self.lib.value(5),101)
                    if fault==5:self.assertEqual(self.lib.value(3),1)
                    self.preserved()

    def test_incompatible_recovery_refuses_without_reading_or_writing(self):
        self.setup_state()
        self.lib.change(1)
        self.lib.restore()
        self.assertEqual(self.lib.value(2),0)
        self.assertEqual(self.output(),b'\xe3'*len(self.raw))
        self.assertEqual(self.lib.value(4),1)
        self.preserved()

    def test_replay_copy_policy_keeps_take_bytes_during_success_and_recovery(self):
        for fail in (False, True):
            with self.subTest(recovery=fail):
                self.setup_state()
                self.lib.replay()
                if fail:
                    self.lib.fault(1, 65536)
                self.assertEqual(self.lib.restore(), 0)
                expected = self.old_raw[1] if fail else self.raw
                self.assertEqual(self.output(), expected[:-276] + b'\xe3' * 276)
                self.assertEqual(self.lib.value(0), int(fail))
                self.preserved()

    def test_live_video_survives_streamed_load_and_ram_recovery_in_both_formats(self):
        first, size = 4090, 9000
        for quick in (False, True):
            for fail in (False, True):
                with self.subTest(quick=quick, recovery=fail):
                    self.setup_state(quick)
                    self.lib.protectVideo(first, size)
                    if fail:
                        self.lib.fault(1, 65536)
                    self.assertEqual(self.lib.restore(), 0)
                    expected = self.old_raw[1] if fail else self.raw
                    self.assertEqual(self.output(), expected[:first] + b'\xe3' * size + expected[first+size:])
                    self.assertEqual(self.lib.value(0), int(fail))
                    self.preserved()


if __name__ == '__main__':unittest.main()
