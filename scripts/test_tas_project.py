"""Run the production TAS coordinator against bounded memory and SD fakes."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class TasProjectTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        source = Path(cls.tmp.name) / "tas.cpp"
        source.write_text(r"""
#include "susamune/savestate.hxx"
#include "susamune/practice_session.hxx"
#include "susamune/state_storage.hxx"
#include "susamune/state_compatibility.h"
extern "C" void *memcpy(void*d,const void*s,size_t n){for(size_t i=0;i<n;++i)((volatile u8*)d)[i]=((const u8*)s)[i];return d;}
extern "C" void *memset(void*d,u32 v,size_t n){for(size_t i=0;i<n;++i)((volatile u8*)d)[i]=(u8)v;return d;}
struct SavedFile {PracticeSession::SavestateData data;SusamuneStateArchiveHeader header;};
static SavedFile fileData[32];
struct SavedTape {SusamuneTasTakeData data;SusamuneStateArchiveHeader header;u32 frames[4096*4];SusamuneTasTransition transitions[32];};
static SavedTape tapeData[32];
static u32 liveFrameWords[4096*4],slotScenes[3],livePosition,currentScene,startScene,nextFileId;
static SusamuneTasTransition liveTransitions[32];
static u32 liveTransitionCount,tapeExportCount,tapeImportCount,saveCount,failTapeExport,restoreTakeCount;
static bool takePresent,tapePayloadFails,restoreCheckpointFails,worldLoadFails,recordedScene;
static u32 worldLoadAttempts,continueCalls;
static SavestateManager::SlotInfo memory[3];
static PracticeSession::SavestateData stateData[3];
static SavestateManager::TransferResult transfer;
static StateStorage::Result response;
static SusamuneTasManifest published,projects[32];
static u32 nextProjectId,beginCount;
static bool failBegin,failCommit;
static SusamuneStateCatalog catalogData;
static bool transferReady,responseReady,tapeAttached,tapeRecord,tapePause,saveFails,compatible,canStart,readyCheckpoint;
static u32 nextGeneration,exportCount,commitCount,importCount,loadCount,clearCount,failExport,liveFrames,liveKey[2],liveRevision,lastLoaded;
static u32 ordinarySave=2,ordinaryLoad=1;
static const char*runtimeStatus="fixture practice status";
static bool exportContextValid(const SusamuneTasRequest*request){
 if(!SusamuneTasRequestValid(request)||!request->projectId||request->projectId>=32)return false;
 const auto&prior=projects[request->projectId];
 return request->projectGeneration==prior.generation&&request->expectedProjectCrc==prior.checksum;
}
SavestateManager::SavestateManager(){}
static SavestateManager manager;
SavestateManager*gSavestateMgr=&manager;
SavestateManager::SlotInfo SavestateManager::slotInfo(u32 slot)const{return slot<3?memory[slot]:SlotInfo{};}
bool SavestateManager::practiceData(u32 slot,PracticeSession::SavestateData*out)const{if(slot>=3||!memory[slot].valid)return false;*out=stateData[slot];return true;}
bool SavestateManager::diskBusy(){return false;}
const char*SavestateManager::sdStatus()const{return "fake transfer refusal";}
bool SavestateManager::projectCompatible(const SusamuneTasManifest&)const{return compatible;}
u32 SavestateManager::currentSceneKey()const{return currentScene;}
u32 SavestateManager::slotSceneKey(u32 slot)const{return slot<3&&memory[slot].valid?slotScenes[slot]:0;}
bool SavestateManager::saveSlotExplicit(u32 slot,bool rng,bool omit){
 ++saveCount;
 if(saveFails||slot>=3)return false;
 memory[slot]={true,2,0,++nextGeneration,6000};slotScenes[slot]=currentScene;stateData[slot]={};auto&d=stateData[slot];
 d.flags=1|(rng?2:0)|(!omit&&tapeAttached?4:0);d.stateKey[1]=nextGeneration;
 if(d.flags&4){d.frames=livePosition;d.originKey[0]=liveKey[0];d.originKey[1]=liveKey[1];}return true;
}
bool SavestateManager::clearSlot(u32 slot,u32 gen){if(slot>=3||memory[slot].generation!=gen)return false;memory[slot].valid=false;memory[slot].generation=++nextGeneration;++clearCount;return true;}
bool SavestateManager::exportSlotExplicit(u32 slot,u32 gen,const SusamuneTasRequest*request){
 if(!exportContextValid(request)||!memory[slot].valid||memory[slot].generation!=gen)return false;
 const u32 id=++nextFileId;++exportCount;transfer={};transfer.command=SUSAMUNE_STATE_CMD_EXPORT;transfer.id=id;transfer.slot=slot;transfer.generation=gen;
 transfer.status=exportCount==failExport?SUSAMUNE_STATE_IO_ERROR:SUSAMUNE_STATE_OK;
 transfer.header.gameId=0x474D5350;transfer.header.buildCrc=SUSAMUNE_STATE_COMPATIBILITY_ID;transfer.header.configId=20;transfer.header.sceneKey=slotScenes[slot];
 transfer.header.headerCrc=id*11;transfer.header.packedSize=memory[slot].packedBytes;
 fileData[id]={stateData[slot],transfer.header};transferReady=true;return true;
}
bool SavestateManager::importSlotExplicit(u32 slot,u32 gen,u32 id,u32 crc,u32 packed,const SusamuneTasRequest*request,const SusamuneTasManifest*manifest){
 if(!SusamuneTasRequestValid(request)||slot>=3||memory[slot].generation!=gen||id>=32||fileData[id].header.headerCrc!=crc)return false;
 ++importCount;transfer={};transfer.command=SUSAMUNE_STATE_CMD_IMPORT;transfer.id=id;transfer.slot=slot;transfer.header=fileData[id].header;
 const auto &data=fileData[id].data;
 const bool semantic=(data.flags&3)==3 && data.frames==manifest->components[request->role].frames &&
  (request->role ? ((data.flags&4)&&data.originKey[0]==manifest->startKey[0]&&data.originKey[1]==manifest->startKey[1]) :
   (!(data.flags&4)&&data.stateKey[0]==manifest->startKey[0]&&data.stateKey[1]==manifest->startKey[1]));
 if(!semantic){transfer.status=SUSAMUNE_STATE_BAD_FILE;transferReady=true;return true;}
 // Model the full-pool staging refusal unless a destination is already free.
 bool full=true;for(u32 i=0;i<3;++i)full=full&&memory[i].valid;
 if(full){transfer.status=SUSAMUNE_STATE_FULL;}else{
 memory[slot]={true,2,0,++nextGeneration,packed};slotScenes[slot]=fileData[id].header.sceneKey;stateData[slot]=fileData[id].data;transfer.generation=nextGeneration;
 }transferReady=true;return true;
}
bool SavestateManager::takeTransferResult(TransferResult&out){if(!transferReady)return false;out=transfer;transferReady=false;return true;}
bool SavestateManager::loadSlot(u32 slot,u32 gen){++worldLoadAttempts;if(worldLoadFails||!memory[slot].valid||memory[slot].generation!=gen)return false;
 ++loadCount;lastLoaded=slot;const auto&d=stateData[slot];tapeAttached=takePresent=(d.flags&4)!=0;liveFrames=livePosition=d.frames;
 liveKey[0]=d.originKey[0];liveKey[1]=d.originKey[1];++liveRevision;return true;
}
namespace PracticeSession {
bool projectSavestateMatches(const SavestateData&d,const u32(&key)[2],u32 role,u32 frames){
 return (d.flags&3)==3&&d.frames==frames&&(role?((d.flags&4)&&d.originKey[0]==key[0]&&d.originKey[1]==key[1]):
 (!frames&&!(d.flags&4)&&d.stateKey[0]==key[0]&&d.stateKey[1]==key[1]));}
bool available(){return true;}bool projectAvailable(){return canStart;}bool starting(){return false;}
bool atRecordedScene(){return recordedScene;}
bool recording(){return tapeRecord;}bool checkpointReady(){return readyCheckpoint&&tapeAttached;}
bool attachedTo(const u32(&key)[2]){return tapeAttached&&key[0]==liveKey[0]&&key[1]==liveKey[1];}
bool takeBelongsTo(const u32(&key)[2]){return takePresent&&key[0]==liveKey[0]&&key[1]==liveKey[1];}
bool captureTake(SusamuneTasTakeData&out,StateCodec::ReadSpan(&spans)[2]){
 if(!takePresent)return false;
 out={};out.version=SUSAMUNE_TAS_TAPE_VERSION;out.frames=liveFrames;out.position=livePosition;
 out.settingsHash=20;out.startFingerprint=123;out.frameHash=SusamuneStateCrc(liveFrameWords,liveFrames*16);
 out.transitionCount=liveTransitionCount;out.transitionHash=SusamuneStateCrc(liveTransitions,liveTransitionCount*16);
 out.startScene=startScene;out.endScene=currentScene;out.originKey[0]=liveKey[0];out.originKey[1]=liveKey[1];
 spans[0]={liveFrameWords,liveFrames*16};spans[1]={liveTransitions,liveTransitionCount*16};return true;
}
bool restoreTake(const SusamuneTasTakeData&data,const void*frames,const void*transitions,const SavestateData*checkpoint){
 ++restoreTakeCount;
 if(!SusamuneTasTakeValid(&data)||(checkpoint&&(restoreCheckpointFails||checkpoint->frames>data.frames)))return false;
 memcpy(liveFrameWords,frames,data.frames*16);memcpy(liveTransitions,transitions,data.transitionCount*16);
 liveFrames=data.frames;livePosition=checkpoint?checkpoint->frames:data.position;liveTransitionCount=data.transitionCount;
 liveKey[0]=data.originKey[0];liveKey[1]=data.originKey[1];startScene=data.startScene;
 takePresent=true;tapeAttached=checkpoint!=nullptr;tapeRecord=false;tapePause=true;++liveRevision;return true;
}
u32 editRevision(){return liveRevision;}
u32 takePosition(){return livePosition;}
bool requestRecordFrom(u32 slot,u32 gen){if(!memory[slot].valid||memory[slot].generation!=gen)return false;
 takePresent=tapeAttached=tapeRecord=tapePause=true;liveFrames=livePosition=liveTransitionCount=0;startScene=currentScene;liveKey[0]=stateData[slot].stateKey[0];liveKey[1]=stateData[slot].stateKey[1];++liveRevision;return true;}
void pauseEditing(){tapeRecord=false;tapePause=true;}
void pauseForCheckpoint(){tapePause=true;}
bool requestBeginning(){if(!takePresent)return false;livePosition=0;tapeAttached=tapePause=true;tapeRecord=false;return true;}
bool requestContinue(){++continueCalls;tapeRecord=true;return tapeAttached;}
bool requestPlayback(){return takePresent;}
const char*status(){return runtimeStatus;}
}
namespace StateStorage {
bool available(){return true;}bool busy(){return false;}
bool startTapeExport(const SusamuneStateArchiveHeader&identity,const SusamuneTasTakeData&data,
                     const void*frames,const void*transitions,const SusamuneTasRequest&request){
 if(!exportContextValid(&request)||request.role!=SUSAMUNE_TAS_TAPE_ROLE||!SusamuneTasTakeValid(&data))return false;
 const u32 id=++nextFileId;++tapeExportCount;auto&file=tapeData[id];file.data=data;file.header=identity;
 memcpy(file.frames,frames,data.frames*16);memcpy(file.transitions,transitions,data.transitionCount*16);
 auto&h=file.header;h.magic=SUSAMUNE_STATE_ARCHIVE_MAGIC;h.version=SUSAMUNE_STATE_ARCHIVE_VERSION;
 h.headerSize=sizeof(h);h.metadataSize=sizeof(data);h.packedSize=h.rawSize=SusamuneTasTakeBytes(&data);
 h.snapshotVersion=SUSAMUNE_TAS_TAPE_SNAPSHOT;h.metadataCrc=SusamuneStateCrc(&data,sizeof(data));h.headerCrc=SusamuneStateHeaderCrc(&h);
 response={};response.command=SUSAMUNE_STATE_CMD_EXPORT;response.id=id;response.header=h;
 response.status=tapeExportCount==failTapeExport?SUSAMUNE_STATE_IO_ERROR:SUSAMUNE_STATE_OK;responseReady=true;return true;
}
bool startTapeImport(const SusamuneTasManifest&manifest){
 if(!SusamuneTasManifestValid(&manifest)||manifest.tape.componentId>=32)return false;
 ++tapeImportCount;response={};response.command=SUSAMUNE_STATE_CMD_IMPORT;response.id=manifest.tape.componentId;
 response.header=tapeData[response.id].header;
 if(response.header.headerCrc!=manifest.tape.headerCrc)response.status=SUSAMUNE_STATE_STALE;
 responseReady=true;return true;
}
bool tapePayload(const Result&result,SusamuneTasTakeData&data,const void*&frames,const void*&transitions){
 if(tapePayloadFails||result.id>=32||!SusamuneTasTapeMetadataValid(&result.header,&tapeData[result.id].data))return false;
 data=tapeData[result.id].data;frames=tapeData[result.id].frames;transitions=tapeData[result.id].transitions;return true;
}
bool takeResult(Result&out){if(!responseReady||response.command>=SUSAMUNE_STATE_CMD_TAS_BEGIN)return false;out=response;responseReady=false;return true;}
bool projectBegin(const char*){++beginCount;if(failBegin)return false;
 response={};response.command=SUSAMUNE_STATE_CMD_TAS_BEGIN;response.id=++nextProjectId;responseReady=true;return true;}
bool projectRead(u32 id,u32 crc){response={};response.command=SUSAMUNE_STATE_CMD_TAS_READ;response.project=published;
 if(id!=published.projectId||crc!=published.checksum)response.status=SUSAMUNE_STATE_STALE;responseReady=true;return true;}
bool projectCommit(const SusamuneTasManifest&value,u32 old){
 if(failCommit||!SusamuneTasManifestValid(&value)||value.projectId>=32)return false;
 const auto&prior=projects[value.projectId];
 if(old!=prior.checksum||value.generation!=prior.generation+1)return false;
 if(prior.projectId&&(value.buildCrc!=prior.buildCrc||value.configId!=prior.configId||value.gameId!=prior.gameId))return false;
 for(u32 i=0;i<3;i++)if(value.components[i].componentId){
  const auto&h=fileData[value.components[i].componentId].header;
  if(h.buildCrc!=value.buildCrc||h.configId!=value.configId||h.gameId!=value.gameId||h.headerCrc!=value.components[i].headerCrc)return false;
 }
 if(tapeData[value.tape.componentId].header.buildCrc!=value.buildCrc)return false;
 ++commitCount;projects[value.projectId]=published=value;response={};response.command=SUSAMUNE_STATE_CMD_TAS_COMMIT;responseReady=true;return true;
}
bool projectRename(u32 id,u32 crc,const char*name){
 response={};response.command=SUSAMUNE_STATE_CMD_TAS_RENAME;response.id=id;
 if(id!=published.projectId||crc!=published.checksum)response.status=SUSAMUNE_STATE_STALE;
 else {++published.generation;memset(published.name,0,32);for(u32 i=0;name[i]&&i<31;++i)published.name[i]=name[i];
 published.checksum=SusamuneTasManifestCrc(&published);projects[id]=published;response.project=published;}
 responseReady=true;return true;}
bool projectDelete(u32 id,u32 crc){response={};response.command=SUSAMUNE_STATE_CMD_TAS_DELETE;response.id=id;
 if(id!=published.projectId||crc!=published.checksum)response.status=SUSAMUNE_STATE_STALE;
 else {memset(&projects[id],0,sizeof(projects[id]));memset(&published,0,sizeof(published));}responseReady=true;return true;}
bool projectCatalog(u32){response={};response.command=SUSAMUNE_STATE_CMD_TAS_CATALOG;responseReady=true;return true;}
bool takeProjectResult(Result&out){if(!responseReady||response.command<SUSAMUNE_STATE_CMD_TAS_BEGIN)return false;out=response;responseReady=false;return true;}
bool catalogReady(){return true;}const SusamuneStateCatalog&catalog(){return catalogData;}
}
""" + '\n'.join(line for line in (ROOT / 'src/tas_project.cpp').read_text().splitlines() if not line.startswith('#pragma clang section')) + r"""
extern "C" __declspec(dllexport) void reset(){
 memset(memory,0,sizeof(memory));memset(stateData,0,sizeof(stateData));memset(fileData,0,sizeof(fileData));
 memset(tapeData,0,sizeof(tapeData));memset(liveFrameWords,0,sizeof(liveFrameWords));memset(liveTransitions,0,sizeof(liveTransitions));memset(slotScenes,0,sizeof(slotScenes));
 memset(&published,0,sizeof(published));memset(projects,0,sizeof(projects));memset(&catalogData,0,sizeof(catalogData));
 nextProjectId=6;beginCount=0;failBegin=failCommit=false;
 transferReady=responseReady=tapeAttached=tapeRecord=tapePause=saveFails=false;
 compatible=canStart=readyCheckpoint=recordedScene=true;nextGeneration=100;
 takePresent=tapePayloadFails=restoreCheckpointFails=worldLoadFails=false;worldLoadAttempts=continueCalls=0;
 livePosition=nextFileId=liveTransitionCount=tapeExportCount=tapeImportCount=saveCount=failTapeExport=restoreTakeCount=0;
 currentScene=startScene=0x2000000;
 exportCount=commitCount=importCount=loadCount=clearCount=failExport=liveFrames=liveRevision=0;
 liveKey[0]=liveKey[1]=0;lastLoaded=99;ordinarySave=2;ordinaryLoad=1;runtimeStatus="fixture practice status";
 memset(TasProject::sRefs,0,sizeof(TasProject::sRefs));memset(TasProject::sPlan,0,sizeof(TasProject::sPlan));
 memset(&TasProject::sSaved,0,sizeof(TasProject::sSaved));memset(&TasProject::sPending,0,sizeof(TasProject::sPending));
 memset(TasProject::sPublishedKeys,0,sizeof(TasProject::sPublishedKeys));
 TasProject::sSavedRevision=TasProject::sCurrent=0;TasProject::sActive=TasProject::sDirty=TasProject::sCatalogReady=false;
 TasProject::sHaveCheckpoint=false;memset(&TasProject::sLoadedCheckpoint,0,sizeof(TasProject::sLoadedCheckpoint));
 TasProject::finish("ready");
}
extern "C" __declspec(dllexport) void ordinary(u32 slot){memory[slot]={true,1,2,++nextGeneration,1234};slotScenes[slot]=0x1000200;stateData[slot]={};stateData[slot].stateKey[1]=nextGeneration;}
extern "C" __declspec(dllexport) u32 action(u32 code,u32 arg){switch(code){case 0:return TasProject::newProject();case 1:return TasProject::saveCheckpoint(arg);case 2:return TasProject::save("Example TAS");case 3:return TasProject::open(published.projectId,published.checksum);case 4:return TasProject::loadCheckpoint(arg);case 5:return TasProject::continueEditing();case 6:return TasProject::replace(arg,memory[arg].generation);case 7:TasProject::cancelReplacement();return 1;
 case 8:return TasProject::rename(published.projectId,published.checksum,"Renamed TAS");
 case 9:return TasProject::remove(published.projectId,published.checksum);
 case 10:return TasProject::rename(published.projectId,published.checksum^1,"Stale");
 case 11:return TasProject::remove(published.projectId,published.checksum^1);case 12:return TasProject::replay();case 13:return TasProject::confirmCheckpointOverwrite(arg);case 14:return TasProject::dispatchShortcut((BindId)arg);
 default:return 0;}}
extern "C" __declspec(dllexport) void tick(){TasProject::update();TasProject::afterDraw();}
extern "C" __declspec(dllexport) void append(){if(livePosition<4096){liveFrameWords[livePosition*4]=1000+liveRevision;liveFrames=++livePosition;++liveRevision;}tapeRecord=true;}
extern "C" __declspec(dllexport) void option(u32 code,u32 value){switch(code){case 0:saveFails=value;break;case 1:failExport=value;break;case 2:compatible=value;break;case 3:canStart=value;break;case 4:readyCheckpoint=value;break;case 5:fileData[published.components[1].componentId].data.originKey[1]=value;break;case 6:memory[TasProject::sRefs[0].slot].packedBytes=value;break;case 7:liveKey[1]=value;break;case 8:runtimeStatus="Playback finished - fingerprints matched";break;
 case 9:currentScene=value;break;case 10:tapeAttached=value;break;case 11:failTapeExport=value;break;case 12:tapePayloadFails=value;break;
 case 13:restoreCheckpointFails=value;break;case 14:livePosition=value;break;case 15:takePresent=value;break;
 case 16:liveTransitions[liveTransitionCount++]={(u16)liveFrames,0,startScene,currentScene,value};++liveRevision;break;
 case 17:tapeData[published.tape.componentId].data.originKey[1]=value;break;
 case 18:tapeData[published.tape.componentId].data.frames=value;break;
 case 19:worldLoadFails=value;break;case 20:recordedScene=value;break;
 case 21:failBegin=value;break;case 22:failCommit=value;break;
 }}
extern "C" __declspec(dllexport) u32 value(u32 code){switch(code){case 0:return TasProject::active();case 1:return TasProject::busy();case 2:return TasProject::replacementNeeded();case 3:return exportCount;case 4:return commitCount;case 5:return importCount;case 6:return loadCount;case 7:return clearCount;case 8:return published.generation;case 9:return published.componentCount;case 10:return published.currentRole;case 11:return TasProject::dirty();case 12:return liveFrames;case 13:return ordinarySave;case 14:return ordinaryLoad;case 15:return lastLoaded;case 16:return TasProject::named();case 17:return TasProject::checkpointOverwritePending();
 case 18:return tapeExportCount;case 19:return tapeImportCount;case 20:return saveCount;case 21:return livePosition;case 22:return tapeAttached;
 case 23:return takePresent;case 24:return published.tapeFrames;case 25:return published.checksum;case 26:return restoreTakeCount;
 case 27:return liveTransitionCount;case 28:return currentScene;case 29:return worldLoadAttempts;case 30:return continueCalls;
 case 31:return beginCount;case 32:return TasProject::sSaved.projectId;case 33:return published.projectId;case 34:return published.buildCrc;default:return 0;}}
extern "C" __declspec(dllexport) void legacy(u32 build){
 published.buildCrc=build;
 for(u32 role=0;role<3;++role)if(published.components[role].componentId){
  auto&h=fileData[published.components[role].componentId].header;h.buildCrc=build;
  published.components[role].headerCrc=h.headerCrc=SusamuneStateHeaderCrc(&h);
 }
 auto&h=tapeData[published.tape.componentId].header;h.buildCrc=build;
 published.tape.headerCrc=h.headerCrc=SusamuneStateHeaderCrc(&h);
 published.checksum=SusamuneTasManifestCrc(&published);projects[published.projectId]=published;
}
extern "C" __declspec(dllexport) u32 projectHash(u32 id){
 if(id>=32)return 0;const auto&p=projects[id];u32 hash=SusamuneStateCrcUpdate(0xffffffffu,&p,sizeof(p));
 for(u32 role=0;role<3;++role)if(p.components[role].componentId)
  hash=SusamuneStateCrcUpdate(hash,&fileData[p.components[role].componentId],sizeof(SavedFile));
 if(p.tape.componentId)hash=SusamuneStateCrcUpdate(hash,&tapeData[p.tape.componentId],sizeof(SavedTape));
 return ~hash;
}
extern "C" __declspec(dllexport) u32 frameWord(u32 frame){return liveFrameWords[frame*4];}
extern "C" __declspec(dllexport) u32 loadable(u32 role){return TasProject::checkpoint(role).loadableHere;}
extern "C" __declspec(dllexport) u32 present(u32 role){return TasProject::checkpoint(role).present;}
extern "C" __declspec(dllexport) u32 checkpointFrames(u32 role){return TasProject::checkpoint(role).frames;}
extern "C" __declspec(dllexport) const char*projectName(){return TasProject::name();}
extern "C" __declspec(dllexport) u32 generation(u32 slot){return memory[slot].generation;}
extern "C" __declspec(dllexport) u32 role(u32 role){return TasProject::sRefs[role].slot;}
extern "C" __declspec(dllexport) u32 allowed(u32 slot){return TasProject::replacementAllowed(slot);}
extern "C" __declspec(dllexport) const char*status(){return TasProject::status();}
""", encoding="utf-8")
        dll=source.with_suffix('.dll')
        result=subprocess.run([str(ROOT/'toolchain/clang++.exe'),'--target=x86_64-pc-windows-msvc','-shared','-nostdlib','-fuse-ld=lld','-Wl,/noentry','-O2','-std=c++17','-I',str(ROOT/'include'),str(source),'-o',str(dll)],capture_output=True,text=True)
        if result.returncode: raise AssertionError(result.stderr)
        cls.lib=C.CDLL(str(dll));cls.lib.status.restype=C.c_char_p
        cls.lib.projectName.restype=C.c_char_p
        cls.addClassCleanup(lambda:C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))
    def setUp(self): self.lib.reset()
    def finish(self):
        for _ in range(30):
            if not self.lib.value(1) or self.lib.value(2):break
            self.lib.tick()
        self.assertFalse(self.lib.value(1), self.lib.status())
    def new(self): self.assertTrue(self.lib.action(0,0));self.finish()
    def save(self): self.assertTrue(self.lib.action(2,0));self.finish()
    def test_new_captures_beginning_without_touching_other_slots_or_selections(self):
        self.lib.ordinary(0);before=self.lib.generation(0);self.new()
        self.assertEqual(self.lib.role(0),1);self.assertEqual(self.lib.generation(0),before)
        self.assertEqual((self.lib.value(13),self.lib.value(14)),(2,1))
    def test_new_project_reuses_its_own_slots_after_discard(self):
        self.new();self.lib.append();self.lib.action(1,1);self.finish();self.lib.action(1,2);self.finish()
        start=self.lib.role(0);self.new()
        self.assertEqual(self.lib.role(0),start);self.assertEqual(self.lib.value(7),2)
        self.assertFalse(self.lib.value(2))
    def test_new_full_memory_requires_explicit_replacement(self):
        for i in range(3):self.lib.ordinary(i)
        before=[self.lib.generation(i) for i in range(3)]
        self.assertTrue(self.lib.action(0,0));self.assertTrue(self.lib.value(2))
        self.lib.action(7,0);self.assertEqual([self.lib.generation(i) for i in range(3)],before)
        self.lib.action(0,0);self.lib.action(6,2);self.finish()
        self.assertEqual(self.lib.role(0),2)
        self.assertEqual([self.lib.generation(i) for i in range(2)],before[:2])
    def test_beginning_never_follows_an_unrelated_loaded_take(self):
        self.new();self.lib.append();self.lib.option(7,999)
        self.assertFalse(self.lib.action(4,0))
        self.assertEqual(self.lib.value(6),0)

    def test_beginning_then_new_inputs_can_save_checkpoint_without_continue(self):
        self.lib.ordinary(2);ordinary=self.lib.generation(2)
        self.new()
        for _ in range(6): self.lib.append()
        beginning=self.lib.generation(self.lib.role(0))
        self.assertTrue(self.lib.action(4,0))
        self.assertEqual((self.lib.value(12),self.lib.value(21)),(6,0))
        for _ in range(3): self.lib.append()
        self.assertTrue(self.lib.action(1,1));self.finish()
        self.assertEqual(self.lib.checkpointFrames(1),3)
        self.assertEqual(self.lib.value(30),0)
        self.assertEqual(self.lib.generation(self.lib.role(0)),beginning)
        self.assertEqual(self.lib.generation(2),ordinary)

    def test_save_after_beginning_without_input_retains_full_published_take(self):
        self.new()
        for _ in range(6): self.lib.append()
        before=[self.lib.frameWord(i) for i in range(6)]
        self.assertTrue(self.lib.action(4,0));self.save()
        self.assertEqual(self.lib.value(24),6)
        self.assertEqual(self.lib.value(21),0)
        self.assertEqual([self.lib.frameWord(i) for i in range(6)],before)
        self.assertEqual(self.lib.value(30),0)
        self.assertFalse(self.lib.present(1))

    def test_dirty_checkpoint_survives_lost_beginning_for_discard_warning(self):
        self.new();self.lib.append();self.lib.action(1,1);self.finish();self.save();self.lib.append()
        self.lib.ordinary(self.lib.role(0))
        self.assertFalse(self.lib.value(0))
        self.assertTrue(self.lib.value(11))

    def test_checkpoint_cannot_replace_beginning(self):
        self.new();self.assertFalse(self.lib.action(1,0))
        self.lib.ordinary(1);self.lib.ordinary(2);self.lib.action(1,1)
        self.assertTrue(self.lib.value(2));self.assertFalse(self.lib.allowed(0))
    def test_failed_capture_keeps_previous_project_and_memory(self):
        self.new();self.lib.append();self.save();before=[self.lib.generation(i) for i in range(3)]
        self.lib.option(0,1);self.lib.action(1,1);self.lib.action(13,1);self.finish()
        self.assertEqual([self.lib.generation(i) for i in range(3)],before)
        self.assertTrue(self.lib.value(0));self.assertEqual(self.lib.value(8),1)
    def test_save_publishes_only_after_beginning_and_separate_tape_exports(self):
        self.new();self.lib.append();self.lib.action(2,0)
        self.lib.tick();self.assertEqual(self.lib.value(4),0)
        self.lib.tick();self.assertEqual(self.lib.value(4),0)
        self.assertEqual((self.lib.value(3),self.lib.value(18)),(1,1))
        self.lib.tick();self.assertEqual(self.lib.value(4),1)
        self.finish();self.assertEqual((self.lib.value(3),self.lib.value(9),self.lib.value(10)),(1,1,0))
    def test_runtime_status_follows_playback_completion_but_saved_status_is_kept(self):
        self.new();self.lib.append();self.save()
        saved=self.lib.status();self.lib.option(8,0)
        self.assertEqual(self.lib.status(),saved)
        self.assertTrue(self.lib.action(12,0))
        self.assertIn(b"Playback finished",self.lib.status())

    def test_new_recording_status_updates_after_starting_finishes(self):
        self.new();self.lib.option(8,0)
        self.assertIn(b"Playback finished",self.lib.status())

    def test_repeated_save_reuses_unchanged_beginning(self):
        self.new();self.lib.append();self.save();self.save()
        self.assertEqual(self.lib.value(3),1);self.assertEqual(self.lib.value(18),2);self.assertEqual(self.lib.value(8),2)

    def open_legacy_project(self, build):
        self.new()
        self.lib.append()
        self.assertTrue(self.lib.action(1, 1))
        self.finish()
        self.save()
        self.save()  # A migrated project must not inherit this generation.
        self.lib.legacy(build)
        self.assertTrue(self.lib.action(3, 0))
        self.finish()
        self.assertTrue(self.lib.value(0))
        self.assertEqual(self.lib.value(8), 2)

    def test_legacy_open_edit_and_checkpoint_save_exports_every_component_to_new_project(self):
        for build in (0xEA3C3DFD, 0x96DD313A):
            with self.subTest(build=hex(build)):
                self.lib.reset()
                self.open_legacy_project(build)
                old_id = self.lib.value(33)
                old_hash = self.lib.projectHash(old_id)
                self.lib.append()
                self.assertTrue(self.lib.action(1, 2))
                self.finish()
                state_exports, tape_exports = self.lib.value(3), self.lib.value(18)
                generations = [self.lib.generation(i) for i in range(3)]
                self.assertTrue(self.lib.action(2, 0))
                self.assertEqual(self.lib.value(32), old_id)
                self.finish()
                self.assertEqual(self.lib.projectHash(old_id), old_hash)
                self.assertNotEqual(self.lib.value(33), old_id)
                self.assertEqual(self.lib.value(32), self.lib.value(33))
                self.assertEqual(self.lib.value(34), 0x4D530011)
                self.assertEqual(self.lib.value(8), 1)
                self.assertEqual(self.lib.value(9), 3)
                self.assertEqual(self.lib.value(24), 2)
                self.assertEqual(self.lib.value(3), state_exports + 3)
                self.assertEqual(self.lib.value(18), tape_exports + 1)
                self.assertEqual([self.lib.generation(i) for i in range(3)], generations)
                self.assertFalse(self.lib.value(11))
                # The next save uses ordinary stable-project reuse and generation 2.
                new_id, begins, exports = self.lib.value(33), self.lib.value(31), self.lib.value(3)
                self.save()
                self.assertEqual((self.lib.value(33), self.lib.value(31), self.lib.value(3)),
                                 (new_id, begins, exports))
                self.assertEqual(self.lib.value(8), 2)

    def test_legacy_save_failure_at_each_phase_keeps_original_project_and_can_retry(self):
        for failure in ("begin", "beginning", "checkpoint", "tape", "commit"):
            with self.subTest(failure=failure):
                self.lib.reset()
                self.open_legacy_project(0xEA3C3DFD)
                self.lib.append()
                old_id, old_hash = self.lib.value(33), self.lib.projectHash(self.lib.value(33))
                commits = self.lib.value(4)
                generations = [self.lib.generation(i) for i in range(3)]
                if failure == "begin": self.lib.option(21, 1)
                elif failure == "beginning": self.lib.option(1, self.lib.value(3) + 1)
                elif failure == "checkpoint": self.lib.option(1, self.lib.value(3) + 2)
                elif failure == "tape": self.lib.option(11, self.lib.value(18) + 1)
                else: self.lib.option(22, 1)
                self.lib.action(2, 0)
                self.finish()
                self.assertEqual((self.lib.value(32), self.lib.value(33)), (old_id, old_id))
                self.assertEqual(self.lib.projectHash(old_id), old_hash)
                self.assertEqual(self.lib.value(4), commits)
                self.assertEqual(self.lib.value(8), 2)
                self.assertEqual(self.lib.value(12), 2)
                self.assertTrue(self.lib.value(11))
                self.assertEqual([self.lib.generation(i) for i in range(3)], generations)
                for option in (1, 11, 21, 22): self.lib.option(option, 0)
                self.save()
                self.assertNotEqual(self.lib.value(33), old_id)
                self.assertEqual(self.lib.value(8), 1)
                self.assertEqual(self.lib.projectHash(old_id), old_hash)

    def test_repacked_smaller_beginning_is_exported_to_keep_capacity(self):
        self.new();self.lib.append();self.save();self.lib.option(6,3000);self.save()
        self.assertEqual(self.lib.value(3),2)
    def test_failed_export_keeps_previous_published_project(self):
        self.new();self.lib.append();self.save();self.lib.append();self.lib.option(6,3000);self.lib.option(1,2);self.save()
        self.assertEqual(self.lib.value(8),1);self.assertEqual(self.lib.value(4),1);self.assertTrue(self.lib.value(11))
    def test_no_new_frame_after_save_is_not_dirty(self):
        self.new();self.lib.append();self.save();self.assertFalse(self.lib.value(11))
        self.lib.action(5,0);self.assertFalse(self.lib.value(11));self.lib.append();self.assertTrue(self.lib.value(11))
    def test_full_previous_project_releases_only_owned_slots_then_opens(self):
        self.new();self.lib.append();self.lib.action(1,1);self.finish();self.lib.append();self.lib.action(1,2);self.finish();self.save()
        self.assertTrue(self.lib.action(3,0));self.finish()
        self.assertEqual((self.lib.value(7),self.lib.value(5),self.lib.value(6)),(3,3,1))
        self.assertEqual(self.lib.value(15),self.lib.role(2));self.assertEqual(self.lib.value(12),2)
    def test_open_preserves_unrelated_state(self):
        self.lib.ordinary(2);before=self.lib.generation(2);self.new();self.lib.append();self.lib.action(1,1);self.finish();self.save();self.lib.action(3,0);self.finish()
        self.assertEqual(self.lib.generation(2),before);self.assertEqual(self.lib.value(7),2)
    def test_wrong_mission_rejected_before_any_ram_change(self):
        self.new();self.lib.append();self.save();before=[self.lib.generation(i) for i in range(3)]
        self.lib.option(2,0);self.lib.action(3,0);self.finish()
        self.assertEqual(self.lib.value(5),0);self.assertEqual(self.lib.value(7),0)
        self.assertEqual([self.lib.generation(i) for i in range(3)],before)
    def test_wrong_checkpoint_origin_never_restores_world(self):
        self.new();self.lib.append();self.lib.action(1,1);self.finish();self.save();self.lib.option(5,999);self.lib.action(3,0);self.finish()
        self.assertEqual(self.lib.value(6),0)
    def test_rename_active_project_updates_identity_for_next_save(self):
        self.new();self.lib.append();self.save();self.lib.action(8,0);self.finish()
        self.assertEqual(self.lib.value(8),2);self.save()
        self.assertEqual(self.lib.value(8),3)
        self.assertEqual(self.lib.value(3),1)

    def test_delete_keeps_editor_memory_and_next_save_creates_project(self):
        self.new();self.lib.append();self.save();before=[self.lib.generation(i) for i in range(3)]
        self.lib.action(9,0);self.finish()
        self.assertFalse(self.lib.value(16));self.assertTrue(self.lib.value(0))
        self.assertEqual([self.lib.generation(i) for i in range(3)],before)
        self.save();self.assertEqual(self.lib.value(8),1)

    def test_stale_rename_and_delete_preserve_published_project(self):
        self.new();self.lib.append();self.save()
        for code in (10,11):
            self.lib.action(code,0);self.finish();self.assertEqual(self.lib.value(8),1)
            self.assertTrue(self.lib.value(16))
        self.save();self.assertEqual(self.lib.value(8),2)

    def test_checkpoint_overwrite_cancel_preserves_every_slot(self):
        self.new();self.lib.append();self.lib.action(1,1);self.finish()
        before=[self.lib.generation(i) for i in range(3)]
        self.assertTrue(self.lib.action(1,1));self.assertTrue(self.lib.value(17))
        for _ in range(5):self.lib.tick()
        self.assertEqual([self.lib.generation(i) for i in range(3)],before)
        self.assertTrue(self.lib.action(13,0));self.finish()
        self.assertEqual([self.lib.generation(i) for i in range(3)],before)

    def test_checkpoint_confirmation_pins_target_and_stale_target_is_kept(self):
        self.new();self.lib.append();self.lib.action(1,1);self.finish()
        slot=self.lib.role(1);self.lib.action(1,1);self.lib.ordinary(slot)
        changed=self.lib.generation(slot)
        self.assertFalse(self.lib.action(13,1));self.finish()
        self.assertEqual(self.lib.generation(slot),changed)

    def test_checkpoint_confirmation_cannot_capture_after_losing_its_take(self):
        self.new();self.lib.append();self.lib.action(1,1);self.finish()
        before=[self.lib.generation(i) for i in range(3)]
        self.lib.action(1,1);self.lib.option(7,999)
        self.assertFalse(self.lib.action(13,1));self.finish()
        self.assertEqual([self.lib.generation(i) for i in range(3)],before)

    def test_checkpoint_confirmation_stays_pinned_after_acceptance(self):
        self.new();self.lib.append();self.lib.action(1,1);self.finish()
        slot=self.lib.role(1);self.lib.action(1,1);self.assertTrue(self.lib.action(13,1))
        self.lib.ordinary(slot);changed=self.lib.generation(slot);self.finish()
        self.assertEqual(self.lib.generation(slot),changed)

    def test_shortcuts_use_the_same_prompt_and_do_not_repeat_while_waiting(self):
        self.new();self.lib.append()
        for save_id,role in ((35,1),(37,2)):
            self.assertTrue(self.lib.action(14,save_id));self.finish()
            before=[self.lib.generation(i) for i in range(3)]
            self.assertTrue(self.lib.action(14,save_id));self.assertTrue(self.lib.value(17))
            self.assertTrue(self.lib.action(14,save_id));self.lib.tick()
            self.assertEqual([self.lib.generation(i) for i in range(3)],before)
            self.assertTrue(self.lib.action(13,1));self.finish()
            self.assertNotEqual(self.lib.generation(self.lib.role(role)),before[self.lib.role(role)])
        self.assertFalse(self.lib.action(14,33))
        self.assertTrue(self.lib.action(14,34))
        self.assertTrue(self.lib.action(14,36));self.finish()
        self.assertEqual(self.lib.value(15),self.lib.role(1))
        self.assertTrue(self.lib.action(14,38));self.finish()
        self.assertEqual(self.lib.value(15),self.lib.role(2))
        self.assertTrue(self.lib.action(14,39));self.assertTrue(self.lib.action(14,40))

    def test_not_ready_scene_or_changed_settings_cannot_create_checkpoint(self):
        self.lib.option(3,0);self.assertFalse(self.lib.action(0,0));self.lib.option(3,1);self.new()
        self.lib.option(4,0);self.assertFalse(self.lib.action(1,1))

    def test_late_portal_checkpoint_refusal_keeps_slots_and_whole_tape_saveable(self):
        self.new()
        for _ in range(6): self.lib.append()
        before=[self.lib.generation(i) for i in range(3)]
        captures=self.lib.value(20)
        self.lib.option(20,0)
        self.assertFalse(self.lib.action(1,1))
        self.assertEqual(self.lib.value(20),captures)
        self.assertEqual([self.lib.generation(i) for i in range(3)],before)
        self.save()
        self.assertEqual(self.lib.value(24),6)
        self.assertEqual([self.lib.generation(i) for i in range(3)],before)
        self.lib.option(20,1)
        self.assertTrue(self.lib.action(1,1));self.finish()
        self.assertEqual(self.lib.checkpointFrames(1),6)

    def test_save_needs_no_new_checkpoint_or_free_memory_slot(self):
        self.lib.ordinary(1);self.lib.ordinary(2);self.new();self.lib.append()
        before=[self.lib.generation(i) for i in range(3)];captures=self.lib.value(20)
        self.lib.option(0,1);self.lib.option(4,0);self.save()
        self.assertEqual([self.lib.generation(i) for i in range(3)],before)
        self.assertEqual(self.lib.value(20),captures)
        self.assertEqual((self.lib.value(9),self.lib.value(24)),(1,1))
        self.assertFalse(self.lib.value(2))

    def test_save_after_zone_keeps_detached_recording_name_frames_and_transitions(self):
        self.new();self.lib.append();self.save();self.lib.append()
        self.lib.option(9,0x2F000002);self.lib.option(10,0);self.lib.option(4,0);self.lib.option(16,123)
        before=[self.lib.generation(i) for i in range(3)]
        self.assertEqual(self.lib.projectName(),b'Example TAS');self.assertTrue(self.lib.value(0))
        self.save()
        self.assertEqual((self.lib.value(12),self.lib.value(24),self.lib.value(27)),(2,2,1))
        self.assertEqual([self.lib.generation(i) for i in range(3)],before)
        self.assertEqual((self.lib.value(20),self.lib.value(8),self.lib.value(22)),(1,2,0))
        self.assertFalse(self.lib.value(11));self.assertFalse(self.lib.loadable(0));self.assertTrue(self.lib.present(0))

    def test_tape_export_failure_keeps_previous_manifest_and_live_take(self):
        self.new();self.lib.append();self.save();checksum=self.lib.value(25)
        before=[self.lib.generation(i) for i in range(3)]
        self.lib.append();self.lib.option(11,2);self.save()
        self.assertEqual((self.lib.value(8),self.lib.value(25),self.lib.value(24)),(1,checksum,1))
        self.assertEqual((self.lib.value(12),self.lib.value(4)),(2,1));self.assertTrue(self.lib.value(11))
        self.assertEqual([self.lib.generation(i) for i in range(3)],before)

    def test_open_in_other_area_retains_tape_without_loading_world(self):
        self.lib.ordinary(2);ordinary=self.lib.generation(2);self.new();self.lib.append()
        self.lib.action(1,1);self.finish();self.lib.append();self.lib.append();self.save()
        frames=[self.lib.frameWord(i) for i in range(3)]
        self.lib.option(9,0x2F000002);self.lib.option(10,0);self.lib.option(4,0)
        self.lib.action(3,0);self.finish()
        self.assertEqual((self.lib.value(5),self.lib.value(19),self.lib.value(6)),(2,1,0))
        self.assertEqual((self.lib.value(12),self.lib.value(21),self.lib.value(22),self.lib.value(23)),(3,3,0,1))
        self.assertEqual([self.lib.frameWord(i) for i in range(3)],frames)
        self.assertEqual(self.lib.generation(2),ordinary);self.assertTrue(self.lib.value(0))
        self.assertIn(b'Return to its beginning area',self.lib.status())
        self.assertFalse(self.lib.action(12,0));self.assertIn(b'area where this TAS begins',self.lib.status())
        self.assertEqual(self.lib.value(6),0);self.assertTrue(self.lib.value(23))

    def test_open_at_checkpoint_preserves_full_tape_tail_and_checkpoint_position(self):
        self.new();self.lib.append();self.lib.action(1,1);self.finish()
        for _ in range(4):self.lib.append()
        frames=[self.lib.frameWord(i) for i in range(5)];self.save()
        self.lib.action(3,0);self.finish()
        self.assertEqual((self.lib.value(6),self.lib.value(12),self.lib.value(21),self.lib.value(22)),(1,5,1,1))
        self.assertEqual(self.lib.value(15),self.lib.role(1))
        self.assertEqual([self.lib.frameWord(i) for i in range(5)],frames)
        self.assertTrue(self.lib.action(12,0));self.assertEqual(self.lib.value(12),5)

    def test_checkpoints_in_other_area_are_present_but_refuse_before_world_load(self):
        self.new();self.lib.append();self.lib.action(1,1);self.finish()
        self.lib.option(9,0x2F000002)
        for role in (0,1):
            self.assertTrue(self.lib.present(role));self.assertFalse(self.lib.loadable(role))
            self.assertFalse(self.lib.action(4,role));self.assertFalse(self.lib.value(1))
            self.assertEqual(self.lib.value(6),0);self.assertEqual(self.lib.value(12),1)
        self.assertIn(b'another area',self.lib.status())

    def test_zero_frame_save_and_open_preserve_a_valid_empty_take(self):
        self.new();self.save();self.assertEqual((self.lib.value(9),self.lib.value(24)),(1,0))
        self.lib.action(3,0);self.finish()
        self.assertEqual((self.lib.value(12),self.lib.value(21),self.lib.value(23)),(0,0,1))
        self.assertEqual((self.lib.value(19),self.lib.value(6)),(1,1))

    def test_failed_checkpoint_attachment_falls_back_to_retained_full_take(self):
        self.new();self.lib.append();self.lib.action(1,1);self.finish();self.lib.append();self.save()
        self.lib.option(13,1);self.lib.action(3,0);self.finish()
        self.assertEqual((self.lib.value(26),self.lib.value(12),self.lib.value(22),self.lib.value(23)),(2,2,0,1))
        self.assertIn(b'recording kept',self.lib.status())

    def test_same_scene_owner_refusal_opens_detached_tape_and_can_save_it_again(self):
        self.new();self.lib.append();self.lib.action(1,1);self.finish()
        self.lib.append();self.lib.append();self.save()
        frames=[self.lib.frameWord(i) for i in range(3)]
        self.lib.option(19,1)
        self.assertTrue(self.lib.action(3,0));self.finish()
        self.assertEqual((self.lib.value(29),self.lib.value(6)),(1,0))
        self.assertEqual((self.lib.value(19),self.lib.value(26)),(1,1))
        self.assertEqual((self.lib.value(12),self.lib.value(22),self.lib.value(23)),(3,0,1))
        self.assertEqual([self.lib.frameWord(i) for i in range(3)],frames)
        self.assertTrue(self.lib.value(0));self.assertIn(b'recording kept',self.lib.status())
        self.save()
        self.assertEqual((self.lib.value(8),self.lib.value(24)),(2,3))
        self.assertEqual(self.lib.value(6),0)

    def test_manual_checkpoint_owner_refusal_keeps_live_tape_without_starting_sd_import(self):
        self.new();self.lib.append();self.lib.action(1,1);self.finish();self.lib.append()
        frames=[self.lib.frameWord(i) for i in range(2)]
        before=[self.lib.generation(i) for i in range(3)]
        self.lib.option(19,1)
        self.assertTrue(self.lib.action(4,1));self.finish()
        self.assertEqual((self.lib.value(29),self.lib.value(6),self.lib.value(19)),(1,0,0))
        self.assertEqual([self.lib.frameWord(i) for i in range(2)],frames)
        self.assertEqual([self.lib.generation(i) for i in range(3)],before)
        self.assertTrue(self.lib.value(0));self.assertIn(b'could not be loaded',self.lib.status())

    def test_rejected_imported_tape_is_not_reported_as_an_open_project(self):
        self.new();self.lib.append();self.save();self.lib.option(9,0x2F000002);self.lib.option(12,1)
        self.lib.action(3,0);self.finish()
        self.assertEqual((self.lib.value(26),self.lib.value(6)),(0,0))
        self.assertIn(b'could not be opened or saved',self.lib.status())

    def test_imported_tape_origin_and_length_must_match_the_project(self):
        for field,value in ((17,999),(18,2)):
            with self.subTest(field=field):
                self.lib.reset();self.new();self.lib.append();self.save()
                self.lib.option(9,0x2F000002);self.lib.option(field,value)
                self.lib.action(3,0);self.finish()
                self.assertEqual((self.lib.value(26),self.lib.value(6)),(0,0))
                self.assertIn(b'could not be opened or saved',self.lib.status())

if __name__=='__main__':unittest.main()
