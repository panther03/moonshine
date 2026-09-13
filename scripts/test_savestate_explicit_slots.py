"""Explicit TAS state slots preserve ordinary selections and pin transfer ownership."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_practice_tape import function_source

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT/'src/savestate.cpp'

FIXTURE = r'''
#define private public
#include "susamune/savestate.hxx"
#undef private
#include "susamune/state_pool_memory.h"
typedef long long OSTime;
extern "C" void *memcpy(void*d,const void*s,__SIZE_TYPE__ n){u8*a=(u8*)d;const u8*b=(const u8*)s;while(n--)*a++=*b++;return d;}
extern "C" void *memset(void*d,int c,__SIZE_TYPE__ n){u8*a=(u8*)d;while(n--)*a++=(u8)c;return d;}
extern "C" char *strncpy(char*d,const char*s,__SIZE_TYPE__ n){char*r=d;while(n--)*d++=*s?*s++:0;return r;}
static int snprintf(char*d,__SIZE_TYPE__ n,const char*,...){if(n)*d=0;return 0;}
struct Header {u32 magic;u8 area_id,episode_id;};
struct StoredState {Header header;u32 archiveProfile,generation,rawSize,packedSize,parentEpisode,metadataTag;};
static StoredState sSlots[3],sCandidate;static StateSlotPool sPool;
static u32 metadataTag(const StoredState &s){return s.generation^s.archiveProfile;}
static u32 sActiveSlot,sLoadSlot,sDiskSlot,sDiskGeneration,sDiskPoolUsed,sDiskScene,sStreamId;
static bool sBusy,sAwaitingLoadApproval,sDiskActive,sDiskRestore,sDiskStream,sDiskRecovered;
static bool sExplicitTransfer,sTransferReady;
static SavestateManager::TransferResult sTransferResult;
static u32 sProjectStartKey[2],sProjectRole,sProjectFrames,sProjectScene;
static SusamuneStateCatalogEntry sSelectedSD;
static OSTime sDiskStarted;static const char*sDiskStatus;
static const u32 kSnapshotMagic=0x53544154,kSnapshotVersion=16;
static bool valid,admitted,transportAccept;static u32 calls,notices;
static u32 archiveGameId(){return 0x474D5345;}static u32 archiveBuildId(){return 12;}static u32 archiveSceneKey(){return 13;}
static bool archiveBuildCompatible(u32 build){return build==12;}
static u32 poolCapacity(){return 250000;}static bool validStore(){return valid;}
static bool admitArchiveStage(){return admitted;}static OSTime OSGetTime(){return 101;}
struct Menu{void toast(const char*){++notices;}}menu;static Menu*gMenu=&menu;
namespace StateArchiveProfile{static bool valid(u32 p){return p==19;}static bool reidentify(u32&p,u32 b){return valid(p)&&b==12;}}
namespace StateStorage{
static u32 command,poolOffset,id,crc,size;static bool occupied;
static SusamuneTasRequest context;static bool hasContext;
static SusamuneStateArchiveHeader header;static const void*metadata;
static u32 configId(){return 14;}
static bool accept(u32 cmd,const SusamuneTasRequest*p){
 if(!transportAccept||occupied)return false;++calls;command=cmd;occupied=true;
 hasContext=p!=0;if(p)context=*p;return true;}
static bool startExport(const SusamuneStateArchiveHeader&h,const void*m,u32 offset,const SusamuneTasRequest*p){
 if(!accept(1,p))return false;header=h;metadata=m;poolOffset=offset;return true;}
static bool startImport(u32 i,u32 c,u32 n,u32 offset,const SusamuneTasRequest*p){
 if(!accept(2,p))return false;id=i;crc=c;size=n;poolOffset=offset;return true;}
static bool startWindow(u32 i,u32 c,u32 n,u32,u32,const SusamuneTasRequest*p){
 if(!accept(7,p))return false;id=i;crc=c;size=n;return true;}
}
SavestateManager::SavestateManager(){mLoadPending=false;mLoadWaitFrames=0;}
bool SavestateManager::diskBusy(){return sDiskActive||StateStorage::occupied;}
'''

EXPORTS = r'''
static SavestateManager manager;
static SusamuneTasRequest request={3,7,4,1,55,{0,0},99};
static SusamuneTasManifest manifest;
static void prepareProject(){manifest={};manifest.magic=SUSAMUNE_TAS_MAGIC;manifest.version=SUSAMUNE_TAS_VERSION;
 manifest.projectId=3;manifest.generation=4;manifest.gameId=archiveGameId();manifest.buildCrc=12;
 manifest.configId=14;manifest.sceneKey=13;manifest.currentRole=1;manifest.componentCount=2;
 strncpy(manifest.name,"project",32);manifest.startKey[0]=1;manifest.startKey[1]=2;
 manifest.components[0]={70,76,50000,0,13};manifest.components[1]={71,77,60000,16,13};
 manifest.tape={72,78,260};manifest.tapeFrames=16;
 manifest.checksum=SusamuneTasManifestCrc(&manifest);request.projectId=3;request.projectGeneration=4;
 request.componentId=71;request.expectedProjectCrc=manifest.checksum;request.role=1;
 request.checksum=SusamuneTasRequestCrc(&request);}

extern "C" {
__declspec(dllexport) void reset(){
 memset(&manager,0,sizeof(manager));memset(sSlots,0,sizeof(sSlots));memset(&sPool,0,sizeof(sPool));
 sActiveSlot=1;sLoadSlot=2;sSelectedSD={};sSelectedSD.id=71;sDiskSlot=9;sDiskGeneration=0;
 sBusy=sAwaitingLoadApproval=sDiskActive=sExplicitTransfer=sTransferReady=false;
 sDiskRestore=sDiskStream=sDiskRecovered=StateStorage::occupied=false;
 valid=admitted=transportAccept=true;calls=notices=0;prepareProject();
 for(u32 i=0;i<3;++i){sSlots[i]={{kSnapshotMagic,(u8)(4+i),(u8)(2+i)},19,101+i,80000,40000,3+i};
  sPool.slots[i]={40000*i,40000};}sPool.used=120000;
}
__declspec(dllexport) void block(u32 n){switch(n){case 1:sBusy=true;break;case 2:manager.mLoadPending=true;break;
 case 3:sAwaitingLoadApproval=true;break;case 4:StateStorage::occupied=true;break;
 case 5:sTransferReady=true;break;case 6:valid=false;break;case 7:admitted=false;break;
 case 8:transportAccept=false;break;case 9:sSlots[0].generation++;break;}}
__declspec(dllexport) u32 invoke(u32 import,u32 slot,u32 generation,u32 context){
 return import?manager.importSlotExplicit(slot,generation,71,77,60000,context?&request:0,&manifest):
  manager.exportSlotExplicit(slot,generation,context?&request:0);}
__declspec(dllexport) void projectFault(u32 n){switch(n){case 1:++manifest.checksum;return;
 case 2:++request.projectId;return;case 3:++request.projectGeneration;return;
 case 4:++request.expectedProjectCrc;return;case 5:++manifest.components[1].headerCrc;break;
 case 6:++manifest.components[1].packedBytes;break;case 7:++manifest.sceneKey;break;case 8:manifest.startKey[0]=manifest.startKey[1]=0;break;}
 manifest.checksum=SusamuneTasManifestCrc(&manifest);request.expectedProjectCrc=manifest.checksum;}
__declspec(dllexport) u32 ordinary(u32 import){return import?manager.loadFromSD(71,77,60000):manager.saveToSD("named");}
__declspec(dllexport) void empty(u32){memset(sSlots,0,sizeof(sSlots));memset(&sPool,0,sizeof(sPool));}
__declspec(dllexport) u32 get(u32 n){switch(n){case 0:return sActiveSlot;case 1:return sLoadSlot;
 case 2:return sSelectedSD.id;case 3:return calls;case 4:return sDiskSlot;case 5:return sDiskGeneration;
 case 6:return sExplicitTransfer;case 7:return StateStorage::poolOffset;case 8:return sDiskActive;
 case 9:return StateStorage::hasContext;case 10:return StateStorage::context.componentId;
 case 11:return StateStorage::header.sceneKey;case 12:return StateStorage::metadata==&sCandidate &&
 sCandidate.generation==sSlots[0].generation && sCandidate.metadataTag==metadataTag(sCandidate) && !sSlots[0].metadataTag;
 case 13:return sDiskPoolUsed;case 14:return sDiskScene;case 15:return StateStorage::command;case 16:return sProjectStartKey[0];
 case 17:return sProjectStartKey[1];case 18:return sProjectRole;case 19:return sProjectFrames;}
 return 0;}
__declspec(dllexport) u32 compatible(u32 mismatch){SusamuneTasManifest p={};p.gameId=0x474D5345;p.buildCrc=12;p.sceneKey=13;p.configId=14;
 if(mismatch==1)++p.gameId;if(mismatch==2)++p.buildCrc;if(mismatch==3)++p.sceneKey;if(mismatch==4)++p.configId;
 return manager.projectCompatible(p);}
}
'''


class ExplicitStateSlotTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='moonshine-explicit-slot-')
        cls.addClassCleanup(cls.temp.cleanup)
        source=FIXTURE
        for name in ('SavestateManager::SlotInfo SavestateManager::slotInfo(',
                     'bool SavestateManager::projectCompatible(',
                     'bool SavestateManager::saveToSD(', 'bool SavestateManager::exportSlotExplicit(',
                     'bool SavestateManager::beginSDExport(', 'bool SavestateManager::loadFromSD(',
                     'bool SavestateManager::importSlotExplicit(', 'bool SavestateManager::beginSDLoad('):
            source+=function_source(SOURCE,name)+'\n'
        path=Path(cls.temp.name)/'explicit.cpp';path.write_text(source+EXPORTS)
        proc=subprocess.run([str(ROOT/'toolchain/clang++.exe'),'--target=x86_64-pc-windows-msvc',
            '-shared','-O2','-fno-builtin','-mno-stack-arg-probe','-nostdlib','-fuse-ld=lld','-Wl,/noentry',
            '-I',str(ROOT/'include'),str(path),'-o',str(path.with_suffix('.dll'))],capture_output=True,text=True)
        if proc.returncode:raise AssertionError(proc.stdout+proc.stderr)
        cls.lib=C.CDLL(str(path.with_suffix('.dll')))
        cls.addClassCleanup(lambda:C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))
        cls.lib.get.restype=C.c_uint

    def test_explicit_export_uses_requested_slot_and_project_without_changing_selections(self):
        self.lib.reset();self.assertEqual(self.lib.invoke(0,0,101,1),1)
        self.assertEqual([self.lib.get(i) for i in range(11)],[1,2,71,1,0,101,1,0,1,1,71])
        self.assertEqual(self.lib.get(11),0x04020003)
        self.assertEqual(self.lib.get(12),1)

    def test_explicit_import_pins_target_generation_and_live_pool_without_selection_swapping(self):
        self.lib.reset();self.assertEqual(self.lib.invoke(1,0,101,1),1)
        self.assertEqual([self.lib.get(i) for i in range(11)],[1,2,71,1,0,101,1,120000,1,1,71])
        self.assertEqual([self.lib.get(i) for i in (13,14,15)],[120000,13,2])
        self.assertEqual([self.lib.get(i) for i in (16,17,18,19)],[1,2,1,16])

    def test_busy_stale_invalid_or_unconfirmed_transfers_do_not_change_slot_ownership(self):
        for imported in (0,1):
            for blocker in range(1,10):
                with self.subTest(imported=imported,blocker=blocker):
                    self.lib.reset();self.lib.block(blocker)
                    self.assertEqual(self.lib.invoke(imported,0,101,1),0)
                    self.assertEqual([self.lib.get(i) for i in range(5)],[1,2,71,0,9])
            for slot,gen,context in ((3,101,1),(0,999,1),(0,101,0)):
                self.lib.reset();self.assertEqual(self.lib.invoke(imported,slot,gen,context),0)
                self.assertEqual([self.lib.get(i) for i in range(5)],[1,2,71,0,9])

    def test_project_manifest_or_component_mismatch_refuses_before_import_starts(self):
        for fault in range(1,9):
            self.lib.reset();self.lib.projectFault(fault)
            self.assertEqual(self.lib.invoke(1,0,101,1),0)
            self.assertEqual([self.lib.get(i) for i in range(5)],[1,2,71,0,9])

    def test_empty_import_target_has_pinned_zero_generation_but_cannot_export(self):
        self.lib.reset();self.lib.empty(0)
        self.assertEqual(self.lib.invoke(0,0,0,1),0)
        self.assertEqual(self.lib.invoke(1,0,0,1),1)
        self.assertEqual([self.lib.get(i) for i in (4,5,6)],[0,0,1])

    def test_ordinary_transfers_keep_default_save_slot_and_no_project_context(self):
        for imported in (0,1):
            self.lib.reset();self.assertEqual(self.lib.ordinary(imported),1)
            self.assertEqual([self.lib.get(i) for i in (0,1,2,4,5,6,9)],[1,2,71,1,102,0,0])

    def test_manifest_guard_requires_region_build_settings_but_allows_other_scene_import(self):
        self.assertEqual(self.lib.compatible(0),1)
        for field in (1,2,4):self.assertEqual(self.lib.compatible(field),0)
        self.assertEqual(self.lib.compatible(3),1)

    def test_explicit_save_routes_only_the_named_slot_and_captures_forced_rng_without_settings_write(self):
        save=function_source(SOURCE,'bool SavestateManager::saveSlotExplicit(')
        self.assertNotIn('sActiveSlot',save);self.assertNotIn('sLoadSlot',save)
        self.assertNotIn('gSettings.set',save)
        self.assertIn('slot >= kSlotCount || !validStore()',save)
        self.assertIn('!(forceRng && kStaticRanges[i].gate == SETTING_SAVE_RNG_STATE)',save)
        self.assertIn('captureSavestate(sCandidate.practice, practiceSource, forceRng, omitPracticeTake)',save)
        self.assertIn('sSlots[slot] = sCandidate',save)
        self.assertIn('PracticeSession::onSavestateSaved(slot, sCandidate.generation)',save)
        self.assertIn('return saveSlotExplicit(sActiveSlot)',function_source(SOURCE,'bool SavestateManager::saveState()'))


if __name__=='__main__':unittest.main()
