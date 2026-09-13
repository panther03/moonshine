"""Exercise production SD completion with real codec validation and banked commits."""
import ctypes as C
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import unittest
import zlib

from test_practice_tape import function_source
from test_state_codec import QUICK_BLOCK, reference_quick_frame

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT/'src/savestate.cpp'

FIXTURE = r'''
#include "susamune/state_storage.h"
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
extern "C" int memcmp(const void*a,const void*b,__SIZE_TYPE__ n){const u8*x=(const u8*)a,*y=(const u8*)b;while(n--){if(*x!=*y)return *x-*y;++x;++y;}return 0;}
static int snprintf(char*d,__SIZE_TYPE__ n,const char*,...){if(n)*d=0;return 0;}
static u8 banks[2][50000], staging[4096];alignas(32)static u8 workspace[0x50000];
#undef SUSAMUNE_STATE_STAGING_SIZE
#define SUSAMUNE_STATE_STAGING_SIZE 4096u
static __UINTPTR_TYPE__ kStagingBase=(__UINTPTR_TYPE__)staging;
static StateSlotPool sPool;
static StatePoolMemory sPoolMemory={{banks[0],banks[1]},{50000,50000}};
namespace PracticeSession{struct SavestateData{u32 flags,stateKey[2],originKey[2],frames;};
enum{kMaxFrames=4096,SAVED_PAD=1,SAVED_RNG=2,SAVED_TAKE=4};}
struct StoredState {u32 generation,rawSize,packedSize,adler32,metadataTag;PracticeSession::SavestateData practice;};
static StoredState sSlots[3],sCandidate,returned;
static u32 sDiskSlot,sDiskGeneration,sDiskPoolUsed,sDiskScene,sDurableSlots;
static u32 sPackedChecksums[3],fullDecodes,verifiedDecodes,policyBytes;
static OSTime sDiskStarted;static bool sDiskActive,sDiskRestore,sDiskLoadReady;
static bool sDiskStream,sDiskRecovered,sStreamCommit;
static SusamuneStateArchiveHeader sStreamHeader;
static u32 sStreamOffset,sStreamSize,sStreamSeen,sStreamCrc,sStreamChecksums[5];
static u32 sLoadSlot,sPendingSlot,sPendingGeneration;
static SusamuneStateCatalogEntry sSelectedSD;
static const char*sDiskStatus;
static bool candidateMatches,transportBusy,ready;static u32 scene,rebased,cleared,notified,stores;
static bool muted,interrupts;
struct TMarDirector {enum{STATE_NORMAL=4};u32 mCurState;};
static TMarDirector director;static TMarDirector*gpMarDirector=&director;static bool loading;
static bool inLoadTransition(){return loading||!gpMarDirector;}
static u32 archiveSceneKey(){return scene;}
static bool validStore(){return StateSlotPoolValid(&sPool,100000);}
static u32 metadataTag(const StoredState&s){return s.generation^s.rawSize^s.packedSize^s.adler32;}
static u32 nextGeneration(){return 555;}
static bool archiveCandidateMatches(const SusamuneStateArchiveHeader&){return candidateMatches;}
static bool archiveProjectCandidateMatches(const SusamuneStateArchiveHeader&){return candidateMatches;}
static const char*archiveStatusText(u32){return "rejected";}
static void GXDrawDone(){}
static bool OSDisableInterrupts(){interrupts=true;return true;}
static void OSRestoreInterrupts(bool){interrupts=false;}
static bool muteAudioDma(){muted=true;return true;}
static void unmuteAudioDma(bool){muted=false;}
static void rebaseMissionStopwatch(OSTime){++rebased;}
static void DCStoreRange(void*,u32){++stores;}
static void*codecWorkspace(){return workspace;}
struct Menu{void toast(const char*){++notified;}}menu;static Menu*gMenu=&menu;
namespace PracticeSession{enum{kSavestateSpanCount=0};static bool copySavestateBytes(void*,const void*,u32){return false;}void onSavestateCleared(u32,u32){++cleared;}void cancelLoadHold(){}}
namespace Ghost{enum{kSavestateSpanCount=0};}
namespace StateArchiveProfile{static void copyGameBytes(void*,void*d,const void*s,u32 n){policyBytes+=n;memcpy(d,s,n);}}
static u32 sLiveArchiveProfile;
static StateLiveVideo::Range sLiveVideo = {};
static StateCodec::Status fullDecode(void*w,u32 n,const StateCodec::ReadSpan*s,u32 count,
 const StateCodec::WriteSpan*d,u32 dn,u32 raw,u32 adler,StateCodec::CopyBytes copy,void*ctx){
 ++fullDecodes;return StateCodec::decompress(w,n,s,count,d,dn,raw,adler,copy,ctx);}
static StateCodec::Status verifiedDecode(void*w,u32 n,const StateCodec::ReadSpan*s,u32 count,
 const StateCodec::WriteSpan*d,u32 dn,u32 raw,u32 adler,StateCodec::CopyBytes copy,void*ctx){
 ++verifiedDecodes;return StateCodec::decompressVerified(w,n,s,count,d,dn,raw,adler,copy,ctx);}
namespace StateStorage{
struct Result{u32 command,status,id;SusamuneStateArchiveHeader header;const void*metadata;char name[32];SusamuneStateWindowReceipt window;SusamuneTasManifest project;};
static Result result;
void update(){}
void discardCancelledResult(){if(ready && result.command==SUSAMUNE_STATE_CMD_CANCEL){ready=false;transportBusy=false;}}
bool takeResult(Result&out){if(!ready)return false;out=result;ready=false;transportBusy=false;return true;}
bool busy(){return transportBusy;}
}
class SavestateManager{public:enum{kSlotCount=3};bool mLoadPending;u32 mLoadWaitFrames;
 struct TransferResult {u32 command,status,id,slot,generation;SusamuneStateArchiveHeader header;};
 bool takeTransferResult(TransferResult&);
 static bool diskBusy();void updateDisk();}manager;
static u32 sProjectStartKey[2],sProjectFrames,sProjectRole;
static bool sExplicitTransfer,sTransferReady;static SavestateManager::TransferResult sTransferResult;
'''

EXPORTS = r'''
extern "C" {
__declspec(dllexport) void reset(const void*packed,u32 packedSize,u32 raw,u32 adler,u32 slot){
 memset(banks,0,sizeof(banks));memset(staging,0,sizeof(staging));memset(sSlots,0,sizeof(sSlots));
 for(u32 i=0;i<3;++i){sPool.slots[i]={i*13000,13000};sSlots[i].generation=100+i;
  for(u32 j=0;j<13000;++j)banks[0][i*13000+j]=(u8)(20+i);}
 sPool.used=39000;sDiskSlot=slot;sDiskGeneration=100+slot;sDiskPoolUsed=39000;
 sDiskScene=scene=0x10203;sDiskActive=true;sDiskStarted=17;sDurableSlots=0;
 sExplicitTransfer=sTransferReady=false;
 sDiskRestore=sDiskLoadReady=manager.mLoadPending=false;manager.mLoadWaitFrames=0;
 sLoadSlot=2;sPendingSlot=sPendingGeneration=0;sSelectedSD={};sSelectedSD.id=71;
 fullDecodes=verifiedDecodes=policyBytes=0;
 for(u32 i=0;i<3;++i)sPackedChecksums[i]=packedChecksum(sPool.slots[i].offset,sPool.slots[i].size);
 returned={9,raw,packedSize,adler,0};returned.metadataTag=metadataTag(returned);
 StateStorage::result={};StateStorage::result.command=SUSAMUNE_STATE_CMD_IMPORT;
 StateStorage::result.status=SUSAMUNE_STATE_OK;StateStorage::result.metadata=&returned;
 StateStorage::result.header.metadataSize=sizeof(returned);
 StateStorage::result.header.payloadCrc=SusamuneStateCrc(packed,packedSize);
 u32 first=packedSize<4096?packedSize:4096;memcpy(staging,packed,first);
 StatePoolMemoryCopyIn(&sPoolMemory,sPool.used,(const u8*)packed+first,packedSize-first);
 candidateMatches=ready=transportBusy=true;rebased=cleared=notified=stores=0;muted=interrupts=false;
}
__declspec(dllexport) void change(u32 which){
 if(which==1)++sSlots[sDiskSlot].generation;
 if(which==2)++scene;
 if(which==3)candidateMatches=false;
 if(which==4)--StateStorage::result.header.metadataSize;
 if(which==5)staging[30]^=0x80;
 if(which==6)StateStorage::result.status=SUSAMUNE_STATE_CANCELLED;
 if(which==7)ready=false;
 if(which==8)++sPool.used;
 if(which==9)StateStorage::result.metadata=0;
 if(which==10)sDiskActive=false;
}
__declspec(dllexport) void directRestore(){sDiskRestore=true;}
__declspec(dllexport) void explicitTransfer(){sExplicitTransfer=true;sProjectStartKey[0]=1;sProjectStartKey[1]=2;
 sProjectFrames=16;sProjectRole=1;returned.practice={7,{3,4},{1,2},16};}
__declspec(dllexport) void projectFault(u32 n){switch(n){case 1:++returned.practice.originKey[0];break;
 case 2:++returned.practice.frames;break;case 3:returned.practice.flags&=~2u;break;
 case 4:returned.practice.flags&=~4u;break;case 5:sProjectRole=0;break;}}
__declspec(dllexport) u32 consumeTransfer(u32*out){SavestateManager::TransferResult result;
 if(!manager.takeTransferResult(result))return 0;
 out[0]=result.command;out[1]=result.status;out[2]=result.id;out[3]=result.slot;
 out[4]=result.generation;out[5]=result.header.payloadCrc;return 1;}
__declspec(dllexport) void metadataResult(u32 command,u32 id,u32 status){
 StateStorage::result.command=command;StateStorage::result.id=id;StateStorage::result.status=status;
 memcpy(StateStorage::result.name,"renamed",8);}
__declspec(dllexport) void tick(){manager.updateDisk();}
__declspec(dllexport) u32 get(u32 key){switch(key){case 0:return sPool.used;case 1:return sDurableSlots;
 case 2:return SavestateManager::diskBusy();case 3:return rebased;case 4:return cleared;
 case 5:return muted||interrupts;case 6:return stores;case 7:return sSlots[sDiskSlot].generation;
 case 8:return sDiskLoadReady;case 9:return manager.mLoadPending;case 10:return sPendingSlot;
 case 11:return sPendingGeneration;case 12:return sLoadSlot;case 13:return sSelectedSD.id;
 case 14:return sSelectedSD.name[0];case 15:return sDiskRestore;
 case 16:return fullDecodes;case 17:return verifiedDecodes;case 18:return policyBytes;case 19:return notified;}return 0;}
__declspec(dllexport) u32 trustedChecksum(u32 slot){return sPackedChecksums[slot];}
__declspec(dllexport) void corruptSlot(u32 slot,u32 at){u8 v;
 StatePoolMemoryCopyOut(&sPoolMemory,sPool.slots[slot].offset+at,&v,1);v^=0x80;
 StatePoolMemoryCopyIn(&sPoolMemory,sPool.slots[slot].offset+at,&v,1);}
__declspec(dllexport) void candidateBytes(void*out){u32 first=returned.packedSize<4096?returned.packedSize:4096;
 memcpy(out,staging,first);StatePoolMemoryCopyOut(&sPoolMemory,sDiskPoolUsed,(u8*)out+first,returned.packedSize-first);}
__declspec(dllexport) u32 decodeReady(void*out){StateCodec::ReadSpan spans[3]={};
 u32 first=sCandidate.packedSize<4096?sCandidate.packedSize:4096;
 spans[0]={staging,first};poolReadSpans(sDiskPoolUsed,sCandidate.packedSize-first,spans+1);
 StateCodec::WriteSpan destination={out,sCandidate.rawSize};
 return StateCodec::decompress(workspace,sizeof(workspace),spans,3,&destination,1,
  sCandidate.rawSize,sCandidate.adler32);}
__declspec(dllexport) void slotBytes(u32 slot,void*out){StatePoolMemoryCopyOut(&sPoolMemory,sPool.slots[slot].offset,out,sPool.slots[slot].size);}
__declspec(dllexport) u32 slotSize(u32 slot){return sPool.slots[slot].size;}
__declspec(dllexport) u32 admitted(u32 state,u32 busy){director.mCurState=state;loading=busy;
 return admitArchiveStage();}
}
'''


class SavestateArchiveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=ROOT/'toolchain/clang++.exe'
        if not compiler.exists():raise unittest.SkipTest('Bundled host compiler required')
        cls.temp=tempfile.TemporaryDirectory(prefix='moonshine-sd-commit-')
        cls.addClassCleanup(cls.temp.cleanup)
        source=FIXTURE+'\nnamespace PracticeSession {\n'+function_source(ROOT/'src/practice_session.cpp','bool projectSavestateMatches(')+'\n}\n'
        for name in ('void poolWriteSpans(', 'void poolReadSpans(', 'u32 packedChecksum(',
                     'bool archiveStageReady()', 'bool admitArchiveStage()',
                     'void copyBaseStateBytes(', 'void copyOwnedStateBytes(', 'void copyStateBytes(',
                     'bool SavestateManager::diskBusy()', 'void SavestateManager::updateDisk()',
                     'bool SavestateManager::takeTransferResult('):
            source+=function_source(SOURCE,name)
        load=function_source(SOURCE,'bool SavestateManager::loadSlot(')
        spans=load[load.index('    StateCodec::ReadSpan compressed[3]'):
                   load.index('    StateCodec::WriteSpan destinations[')]
        dispatch=load[load.index('    StateCodec::Status restored;'):
                      load.rindex('    if (restored == StateCodec::COMMIT_FAILED)')]
        # The separate streaming harness covers the recovery transaction.
        dispatch=dispatch[:dispatch.index('    if (fromSD && sDiskStream)')] + '    if (fromSD) {' + dispatch.split('    } else if (fromSD) {',1)[1]
        dispatch=dispatch.replace('StateCodec::decompress(', 'fullDecode(').replace(
            'StateCodec::decompressVerified(', 'verifiedDecode(')
        restore=r'''
extern "C" __declspec(dllexport) u32 restorePayload(u32 slot,u32 direct,void*out){
 const bool fromSD=direct!=0,durable=fromSD||(sDurableSlots&(1u<<slot));
 const StoredState&saved=fromSD?sCandidate:sSlots[slot];
 struct Header{u32 region_count;};Header header={1};Header*h=&header;
 StateCodec::WriteSpan destinations[1]={{out,saved.rawSize}};
''' + spans + dispatch + '\n return restored;\n}\n'
        path=Path(cls.temp.name)/'test.cpp';path.write_text(source+EXPORTS+restore)
        proc=subprocess.run([str(compiler),'--target=x86_64-pc-windows-msvc','-shared','-O2',
            '-fno-builtin','-mno-stack-arg-probe','-nostdlib','-fuse-ld=lld','-Wl,/noentry',
            '-I',str(ROOT/'include'),str(path),str(ROOT/'src/state_codec.cpp'),
            str(ROOT/'src/state_crc.cpp'),'-o',str(path.with_suffix('.dll'))],
            capture_output=True,text=True)
        if proc.returncode:raise RuntimeError(proc.stdout+proc.stderr)
        cls.lib=C.CDLL(str(path.with_suffix('.dll')))
        cls.addClassCleanup(lambda:C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))
        cls.lib.reset.argtypes=[C.c_void_p]+[C.c_uint]*4
        cls.lib.slotBytes.argtypes=[C.c_uint,C.c_void_p]
        cls.lib.candidateBytes.argtypes=[C.c_void_p]
        cls.lib.decodeReady.argtypes=[C.c_void_p]
        cls.lib.restorePayload.argtypes=[C.c_uint,C.c_uint,C.c_void_p]
        cls.lib.trustedChecksum.restype=C.c_uint
        cls.lib.consumeTransfer.argtypes=[C.POINTER(C.c_uint)]

    def setupCandidate(self,slot=1,size=20000,quick=False,fault=None):
        self.raw=random.Random(99).randbytes(size)
        if quick:
            length=QUICK_BLOCK-25
            block=(b'\x1fA\x01\x00'+bytes([255])*(length//255)+bytes([length%255])+b'\x50AAAAA')
            self.packed=(struct.pack('>IIII',0x4D534C34,QUICK_BLOCK,QUICK_BLOCK,len(block))+block+
                         struct.pack('>II',size,size|0x80000000)+self.raw)
            self.raw=b'A'*QUICK_BLOCK+self.raw
            self.assertEqual(reference_quick_frame(self.packed,len(self.raw)),self.raw)
        else:self.packed=zlib.compress(self.raw)
        adler=zlib.adler32(self.raw)
        if fault:
            changed=bytearray(self.packed)
            if fault=='magic':changed[0]^=1
            elif fault=='block_size':changed[4]^=1
            elif fault=='raw_length':changed[11]^=1
            elif fault=='packed_length':changed[12]|=0x80
            elif fault=='lz4_distance':changed[18:20]=b'\0\0'
            elif fault=='truncated':changed.pop()
            elif fault=='appended':changed.append(0)
            elif fault=='adler':adler^=1
            else:raise ValueError(fault)
            self.packed=bytes(changed)
        self.owner=C.create_string_buffer(self.packed)
        self.lib.reset(self.owner,len(self.packed),len(self.raw),adler,slot)

    def slot(self,index):
        b=C.create_string_buffer(self.lib.slotSize(index));self.lib.slotBytes(index,b);return b.raw

    def test_validated_import_commits_only_pinned_slot_and_marks_durable(self):
        for slot in range(3):
            self.setupCandidate(slot);self.assertEqual(self.lib.get(2),1)
            self.lib.tick()
            self.assertEqual(self.slot(slot),self.packed)
            for other in range(3):
                if other!=slot:self.assertEqual(self.slot(other),bytes([20+other])*13000)
            self.assertEqual(self.lib.get(1),1<<slot)
            self.assertEqual(self.lib.get(7),555)
            self.assertEqual(self.lib.trustedChecksum(slot),zlib.crc32(self.packed))
            for other in range(3):
                if other!=slot:self.assertEqual(self.lib.trustedChecksum(other),zlib.crc32(bytes([20+other])*13000))
            self.assertEqual([self.lib.get(i)for i in (2,3,4,5)],[0,1,1,0])
            self.assertEqual([self.lib.get(i)for i in (12,13)],[2,71],
                             'Import must preserve the separately selected Load source')

    def test_stale_slot_scene_profile_size_bad_stream_and_cancel_preserve_all_slots(self):
        for fault in range(1,7):
            with self.subTest(fault=fault):
                self.setupCandidate();self.lib.change(fault);self.lib.tick()
                for slot in range(3):self.assertEqual(self.slot(slot),bytes([20+slot])*13000)
                self.assertEqual([self.lib.get(i)for i in (0,1,2,3,4,5,6)],[39000,0,0,1,0,0,0])
                for slot in range(3):self.assertEqual(self.lib.trustedChecksum(slot),zlib.crc32(bytes([20+slot])*13000))

    def test_missing_receipt_keeps_pool_owned_without_rebase_or_commit(self):
        self.setupCandidate();self.lib.change(7)
        for _ in range(10):self.lib.tick()
        self.assertEqual([self.lib.get(i)for i in (0,1,2,3,4,5,6)],[39000,0,1,0,0,0,0])

    def test_direct_sd_load_validates_but_keeps_all_memory_slots_unchanged(self):
        for slot in range(3):
            with self.subTest(save_slot=slot):
                self.setupCandidate(slot); self.lib.directRestore(); self.lib.tick()
                for saved in range(3):self.assertEqual(self.slot(saved),bytes([20+saved])*13000)
                self.assertEqual([self.lib.get(i)for i in (0,1,2,3,4,5,6)],
                                 [39000,0,1,0,0,0,0])
                self.assertEqual([self.lib.get(i)for i in (8,9,10,11,12,13,15)],
                                 [1,1,3,9,2,71,0])
                candidate=C.create_string_buffer(len(self.packed));self.lib.candidateBytes(candidate)
                self.assertEqual(candidate.raw,self.packed)
                # The receipt is consumed once; ownership lasts until the post-draw queue releases it.
                self.lib.tick();self.lib.tick()
                self.assertEqual([self.lib.get(i)for i in (2,3,4,8,9)],[1,0,0,1,1])

    def test_direct_sd_failed_validation_or_cancel_never_queues_restore_or_changes_slots(self):
        for fault in (1,2,3,4,6,8,9):
            with self.subTest(fault=fault):
                self.setupCandidate();self.lib.directRestore();self.lib.change(fault);self.lib.tick()
                for slot in range(3):self.assertEqual(self.slot(slot),bytes([20+slot])*13000)
                self.assertEqual([self.lib.get(i)for i in (1,2,4,5,6,8,9,12,13,15)],
                                 [0,0,0,0,0,0,0,2,71,0])
                self.assertEqual(self.lib.get(3),int(fault!=10))

    def test_unowned_tape_receipt_is_left_for_project_coordinator(self):
        self.setupCandidate();self.lib.directRestore();self.lib.change(10);self.lib.tick()
        self.assertEqual([self.lib.get(i) for i in (2,3,4,8,9)], [1,0,0,0,0])
        for slot in range(3):self.assertEqual(self.slot(slot),bytes([20+slot])*13000)

    def test_direct_sd_defers_stream_preflight_to_restore_and_bad_stream_writes_nothing(self):
        for corrupt in (False,True):
            with self.subTest(corrupt=corrupt):
                self.setupCandidate();self.lib.directRestore()
                if corrupt:self.lib.change(5)
                self.lib.tick()
                self.assertEqual([self.lib.get(i)for i in (2,8,9)],[1,1,1])
                output=C.create_string_buffer(bytes([0xa5])*len(self.raw),len(self.raw))
                result=self.lib.restorePayload(3,1,output)
                self.assertEqual(result==0,not corrupt)
                self.assertEqual(output.raw,bytes([0xa5])*len(self.raw) if corrupt else self.raw)
                self.assertEqual([self.lib.get(i)for i in (16,17,18)],
                                 [1,0,0 if corrupt else len(self.raw)])
                for slot in range(3):self.assertEqual(self.slot(slot),bytes([20+slot])*13000)
                for slot in range(3):self.assertEqual(self.lib.trustedChecksum(slot),zlib.crc32(bytes([20+slot])*13000))
        update=function_source(SOURCE,'void SavestateManager::updateDisk()')
        self.assertIn('if (valid && !sDiskRestore)',update)
        load=function_source(SOURCE,'bool SavestateManager::loadSlot(')
        self.assertIn('StateCodec::decompress(',load)
        self.assertIn('poolReadSpans(sDiskPoolUsed, saved.packedSize - first, compressed + 1)',load)

    def test_validated_import_mints_trusted_crc_then_ram_restore_uses_verified_copy_policy(self):
        for slot in range(3):
            with self.subTest(slot=slot):
                self.setupCandidate(slot,size=30000);self.lib.tick()
                self.assertEqual(self.lib.trustedChecksum(slot),zlib.crc32(self.packed))
                output=C.create_string_buffer(len(self.raw))
                self.assertEqual(self.lib.restorePayload(slot,0,output),0)
                self.assertEqual(output.raw,self.raw)
                self.assertEqual([self.lib.get(i)for i in (16,17,18)],[0,1,len(self.raw)])
                self.assertEqual(self.slot(slot),self.packed)

    def test_independent_quick_frame_import_and_direct_restore_use_existing_archive_paths(self):
        for direct in (False,True):
            for slot in range(3):
                with self.subTest(direct=direct,slot=slot):
                    self.setupCandidate(slot,size=30000,quick=True)
                    if direct:self.lib.directRestore()
                    self.lib.tick()
                    output=C.create_string_buffer(len(self.raw))
                    self.assertEqual(self.lib.restorePayload(3 if direct else slot,direct,output),0)
                    self.assertEqual(output.raw,self.raw)
                    self.assertEqual([self.lib.get(i)for i in (16,17,18)],
                                     [int(direct),int(not direct),len(self.raw)])
                    for other in range(3):
                        expected=self.packed if not direct and other==slot else bytes([20+other])*13000
                        self.assertEqual(self.slot(other),expected)
                    self.assertEqual(self.lib.get(6),0,'PPC-only commits need no eager whole-pool store')

    def test_malformed_quick_archive_or_adler_never_writes_game_bytes_or_replaces_old_slots(self):
        for direct in (False,True):
            for fault in ('magic','block_size','raw_length','packed_length','lz4_distance',
                          'truncated','appended','adler'):
                with self.subTest(direct=direct,fault=fault):
                    self.setupCandidate(size=30000,quick=True,fault=fault)
                    if direct:self.lib.directRestore()
                    self.lib.tick()
                    if direct:
                        output=C.create_string_buffer(bytes([0xa7])*len(self.raw),len(self.raw))
                        self.assertEqual(self.lib.restorePayload(3,1,output),4)
                        self.assertEqual(output.raw,bytes([0xa7])*len(self.raw))
                    else:self.assertEqual([self.lib.get(i)for i in (0,1,2,4)],[39000,0,0,0])
                    self.assertEqual(self.lib.get(18),0)
                    self.assertEqual(self.lib.get(6),0)
                    for slot in range(3):
                        expected=bytes([20+slot])*13000
                        self.assertEqual(self.slot(slot),expected)
                        self.assertEqual(self.lib.trustedChecksum(slot),zlib.crc32(expected))

    def test_corrupt_ram_crc_refuses_before_any_decode_or_destination_write_across_banks(self):
        for at in (0,1,23999,24000,30005):
            with self.subTest(packed_offset=at):
                self.setupCandidate(1,size=30000);self.lib.tick()
                trusted=self.lib.trustedChecksum(1)
                self.lib.corruptSlot(1,at)
                output=C.create_string_buffer(bytes([0xa5])*len(self.raw),len(self.raw))
                self.assertEqual(self.lib.restorePayload(1,0,output),4)
                self.assertEqual(output.raw,bytes([0xa5])*len(self.raw))
                self.assertEqual([self.lib.get(i)for i in (16,17,18)],[0,0,0])
                self.assertEqual(self.lib.trustedChecksum(1),trusted)
                for slot in (0,2):self.assertEqual(self.slot(slot),bytes([20+slot])*13000)

    def test_project_transfer_reports_validated_import_generation_once_without_generic_toast(self):
        for accepted in (True,False):
            self.setupCandidate(slot=1);self.lib.explicitTransfer()
            if not accepted:self.lib.change(3)
            result=(C.c_uint*6)()
            self.assertEqual(self.lib.consumeTransfer(result),0)
            self.lib.tick()
            self.assertEqual(self.lib.consumeTransfer(result),1)
            self.assertEqual(list(result)[0:5],[2,0 if accepted else 3,0,1,555 if accepted else 101])
            self.assertEqual(result[5],zlib.crc32(self.packed))
            self.assertEqual(self.lib.consumeTransfer(result),0)
            self.assertEqual(self.lib.get(4),int(accepted))
            self.assertEqual(self.lib.get(19),0)
            self.assertEqual(self.lib.get(12),2)
            self.assertEqual(self.lib.get(13),71)

    def test_wrong_project_checkpoint_refuses_before_replacing_any_ram_slot(self):
        for fault in range(1,6):
            self.setupCandidate(slot=1);self.lib.explicitTransfer();self.lib.projectFault(fault)
            before=[self.slot(i) for i in range(3)]
            self.lib.tick();result=(C.c_uint*6)()
            self.assertEqual(self.lib.consumeTransfer(result),1)
            self.assertEqual(list(result)[0:5],[2,3,0,1,101])
            self.assertEqual([self.slot(i) for i in range(3)],before)
            self.assertEqual(self.lib.get(4),0)
            self.assertEqual(self.lib.get(19),0)

    def test_ordinary_transfer_does_not_publish_project_completion(self):
        self.setupCandidate();self.lib.tick();result=(C.c_uint*6)()
        self.assertEqual(self.lib.consumeTransfer(result),0)

    def test_trusted_crc_is_local_and_checked_with_interrupts_off_before_fast_decode(self):
        production=SOURCE.read_text()
        stored=production[production.index('struct StoredState'):production.index('StateSlotPool sPool;')]
        self.assertNotIn('sPackedChecksums',stored)
        constructor=function_source(SOURCE,'SavestateManager::SavestateManager()')
        self.assertIn('memset(sPackedChecksums, 0, sizeof(sPackedChecksums))',constructor)
        save=function_source(SOURCE,'bool SavestateManager::saveSlotExplicit(')
        crc=save.index('sPackedChecksums[slot] = packedChecksum(')
        self.assertLess(save.index('bool fits = compressCandidate('),crc)
        self.assertLess(save.index('if (!fits)'),crc)
        self.assertGreater(save.index('OSRestoreInterrupts(ints)',crc),crc)
        load=function_source(SOURCE,'bool SavestateManager::loadSlot(')
        crc=load.index('packedChecksum(sPool.slots[slot].offset, saved.packedSize) != sPackedChecksums[slot]')
        self.assertLess(load.index('OSDisableInterrupts()'),crc)
        self.assertLess(crc,load.index('StateCodec::decompressVerified(',crc))
        self.assertLess(load.index('StateCodec::decompressVerified(',crc),load.index('OSRestoreInterrupts(ints)',crc))
        self.assertIn('if (restored == StateCodec::COMMIT_FAILED) __builtin_trap()',load)
        clear=function_source(SOURCE,'bool SavestateManager::clearSlot(')
        self.assertIn('sPackedChecksums[slot] = 0',clear)

    def test_failed_direct_sd_restore_rebases_only_the_outer_full_transaction(self):
        load=function_source(SOURCE,'bool SavestateManager::loadSlot(')
        failure_lines=[line.strip()for line in load.splitlines()
                       if 'rebaseMissionStopwatch(restoreStarted)' in line]
        self.assertEqual(failure_lines,
            ['if (!fromSD) rebaseMissionStopwatch(restoreStarted);']*2,
            'Profile and decode failures must leave direct SD timing to its queue owner')
        process=function_source(SOURCE,'void SavestateManager::processPendingLoad()')
        self.assertIn('if (!restored) {\n            rebaseMissionStopwatch(sDiskStarted);',process)
        self.assertEqual(process.count('if (!restored) {\n            rebaseMissionStopwatch(sDiskStarted);'),1)

    def test_rename_and_delete_touch_only_matching_selected_sd_identity_after_success(self):
        header=(ROOT/'include/susamune/state_storage.h').read_text()
        self.assertIn('SUSAMUNE_STATE_CMD_RENAME, SUSAMUNE_STATE_CMD_DELETE',header)
        for command in (5,6):
            for selected in (False,True):
                for success in (False,True):
                    with self.subTest(command=command,selected=selected,success=success):
                        self.setupCandidate();self.lib.metadataResult(command,71 if selected else 72,
                            0 if success else 1);self.lib.tick()
                        self.assertEqual(self.lib.get(13),0 if command==6 and selected and success else 71)
                        self.assertEqual(self.lib.get(14),ord('r') if command==5 and selected and success else 0)
                        for slot in range(3):self.assertEqual(self.slot(slot),bytes([20+slot])*13000)
                        self.assertEqual([self.lib.get(i)for i in (1,2,3,4,6,8,9)],[0,0,1,0,0,0,0])

    def test_game_manifest_and_live_profile_checks_precede_decode_and_commit(self):
        check=function_source(SOURCE,'bool archiveCandidateMatches(')
        for condition in ('!archiveBuildCompatible(file.buildCrc)', 'file.gameId != archiveGameId()',
                'sCandidate.archiveProfile.build != file.buildCrc', 'sCandidate.archiveProfile.config != file.configId',
                'file.snapshotVersion != kSnapshotVersion', 'file.sceneKey != archiveSceneKey()',
                'sCandidate.metadataTag != metadataTag(sCandidate)', '!validSnapshotRegions(&h, begin, end)',
                '!Ghost::savestateRestoreSpans(sCandidate.ghost, ghost)',
                'StateArchiveProfile::matches(sCandidate.archiveProfile, sLiveArchiveProfile)'):
            self.assertIn(condition,check)
        update=function_source(SOURCE,'void SavestateManager::updateDisk()')
        self.assertLess(update.index('archiveCandidateMatches('),update.index('StateCodec::validate('))
        self.assertLess(update.index('StateCodec::validate('),update.index('StateSlotPoolCommitBanked('))
        self.assertLess(update.index('StateSlotPoolCommitBanked('),update.index('sSlots[sDiskSlot] = sCandidate'))
        load=function_source(SOURCE,'bool SavestateManager::loadSlot(')
        self.assertLess(load.index('StateArchiveProfile::matches('),load.index('StateCodec::decompress('))
        self.assertIn('copyStateBytes,\n            durable ? &sLiveArchiveProfile : nullptr',load)

    def test_sd_admission_requires_normal_play_and_reports_refusal(self):
        for state in range(13):
            self.assertEqual(self.lib.admitted(state,0),int(state==4))
            self.assertEqual(self.lib.admitted(state,1),0)
        admission=function_source(SOURCE,'bool admitArchiveStage()')
        self.assertIn('Return to normal play before using SD states',admission)
        self.assertIn('gMenu->toast(sDiskStatus)',admission)
        self.assertIn('beginSDLoad(id, crc, packed, false)',
                      function_source(SOURCE,'bool SavestateManager::loadFromSD('))
        for action in ('beginSDExport','beginSDLoad','refreshSD','renameSD','deleteSD'):
            code=function_source(SOURCE,f'bool SavestateManager::{action}(')
            self.assertLess(code.index('admitArchiveStage()'),code.index('StateStorage::'))


if __name__=='__main__':unittest.main()
