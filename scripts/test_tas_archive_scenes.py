"""Validate imported cross-area project snapshots before retaining their bytes."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_practice_tape import function_source

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/savestate.cpp'
FIXTURE = r'''
#include "susamune/state_storage.h"
static bool archiveBuildCompatible(unsigned int build){return build==123;}
typedef unsigned int u32;
enum {SUSAMUNE_GAME_VERSION=1,kSnapshotVersion=17,kSnapshotMagic=0x53555341};
struct Region {u32 buf_offset;};
struct SavestateHeader {u32 magic,version,game_version,area_id,episode_id,heap_addr,heap_size,region_count;Region regions[2];};
struct Profile {u32 game,build,config,valid;};
struct StoredState {SavestateHeader header;u32 packedSize,rawSize,metadataTag,generation,parentEpisode;Profile archiveProfile;u32 ghost,practice;};
static StoredState sCandidate;static u32 sProjectScene;static bool regionsValid;
static SusamuneStateArchiveHeader file;
static u32 archiveBuildId(){return 123;}static u32 archiveGameId(){return 0x474D534A;}
static u32 metadataTag(const StoredState&){return 678;}
static bool validSnapshotRegions(const SavestateHeader*h,u32 begin,u32 end){
 return regionsValid&&h->region_count==2&&begin==0x80600000&&end==0x80720000;}
namespace StateCodec {struct WriteSpan{void*data;u32 size;};}
namespace StateArchiveProfile {static bool valid(const Profile&p){return p.valid==1;}}
namespace Ghost {enum{kSavestateSpanCount=1};static bool savestateRestoreSpans(u32 n,StateCodec::WriteSpan(&out)[1]){
 if(n>512)return false;out[0]={0,n};return true;}}
namespace PracticeSession {enum{kSavestateSpanCount=1};static bool savestateRestoreSpans(u32 n,StateCodec::WriteSpan(&out)[1]){
 if(n>66048)return false;out[0]={0,n};return true;}}
'''
EXPORTS = r'''
extern "C" {
__declspec(dllexport) void reset(){
 sCandidate={};auto&h=sCandidate.header;h.magic=kSnapshotMagic;h.version=kSnapshotVersion;h.game_version=1;
 h.area_id=0x34;h.episode_id=3;h.heap_addr=0x80600000;h.heap_size=0x120000;h.region_count=2;h.regions[1].buf_offset=1000;
 sCandidate.parentEpisode=7;sCandidate.rawSize=h.heap_size+1000+12+256;sCandidate.packedSize=20000;
 sCandidate.ghost=12;sCandidate.practice=256;sCandidate.generation=1;sCandidate.metadataTag=678;
 sCandidate.archiveProfile={1,123,456,1};sProjectScene=0x34030007;regionsValid=true;
 file={};file.metadataSize=sizeof(sCandidate);file.buildCrc=123;file.gameId=archiveGameId();file.snapshotVersion=17;
 file.sceneKey=sProjectScene;file.packedSize=sCandidate.packedSize;file.rawSize=sCandidate.rawSize;file.configId=456;
}
__declspec(dllexport) u32 validate(u32 fault){switch(fault){
 case 1:++file.sceneKey;break;case 2:++sCandidate.header.area_id;break;case 3:++sCandidate.header.episode_id;break;
 case 4:++sCandidate.parentEpisode;break;case 5:++file.buildCrc;break;case 6:++file.gameId;break;
 case 7:--file.snapshotVersion;break;case 8:sCandidate.header.heap_addr=0x7fffffe0;break;
 case 9:sCandidate.header.heap_size=0xffe00000;break;case 10:sCandidate.header.heap_addr=0x81800000;break;
 case 11:regionsValid=false;break;case 12:++sCandidate.archiveProfile.game;break;
 case 13:++sCandidate.archiveProfile.build;break;case 14:++sCandidate.archiveProfile.config;break;
 case 15:sCandidate.archiveProfile.valid=0;break;case 16:sCandidate.practice=66049;break;
 case 17:sCandidate.ghost=513;break;case 18:++sCandidate.rawSize;++file.rawSize;break;
 case 19:++sCandidate.metadataTag;break;case 20:sCandidate.generation=0;break;
 case 21:sCandidate.header.region_count=0;break;case 22:--file.metadataSize;break;
 }
 return archiveProjectCandidateMatches(file);
}
}
'''


class TasArchiveSceneTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='moonshine-tas-scene-')
        cls.addClassCleanup(cls.temp.cleanup)
        path = Path(cls.temp.name)
        (path/'test.cpp').write_text(FIXTURE + function_source(SOURCE, 'bool archiveProjectCandidateMatches(') + EXPORTS)
        proc = subprocess.run([str(ROOT/'toolchain/clang++.exe'), '--target=x86_64-pc-windows-msvc',
            '-shared', '-O1', '-fno-builtin', '-nostdlib', '-fuse-ld=lld', '-Xlinker', '/noentry',
            '-I', str(ROOT/'include'), str(path/'test.cpp'), '-o', str(path/'test.dll')], capture_output=True, text=True)
        if proc.returncode: raise RuntimeError(proc.stdout + proc.stderr)
        cls.lib = C.CDLL(str(path/'test.dll'))
        cls.addClassCleanup(lambda: C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def test_import_accepts_pinned_other_area_without_dereferencing_its_old_heap(self):
        self.lib.reset()
        self.assertEqual(self.lib.validate(0), 1)

    def test_manifest_metadata_spans_and_owner_identity_are_required(self):
        for fault in range(1,23):
            with self.subTest(fault=fault):
                self.lib.reset()
                self.assertEqual(self.lib.validate(fault), 0)

    def test_actual_restore_still_requires_live_scene_heap_and_owner_match(self):
        body = function_source(SOURCE, 'bool SavestateManager::loadSlot(')
        for guard in ('heapStart != h->heap_addr', 'heapSize != h->heap_size',
                      'h->area_id    != gpApplication.mCurrentScene.mAreaID',
                      'h->episode_id != gpApplication.mCurrentScene.mEpisodeID',
                      'saved.parentEpisode != parentEpisode()',
                      'StateArchiveProfile::matches(saved.archiveProfile, sLiveArchiveProfile)'):
            self.assertIn(guard, body)


if __name__ == '__main__':
    unittest.main()
