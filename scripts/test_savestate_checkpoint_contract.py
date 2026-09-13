"""Exercise state metadata, feedback and copy ownership with production methods."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_practice_tape import function_source

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/savestate.cpp'

FIXTURE = r'''
#include "susamune/qft_timer.hxx"
#include "susamune/iling.hxx"
#include "susamune/ghost.hxx"
#include "susamune/practice_session.hxx"
#include "susamune/state_archive_profile.hxx"
#include "susamune/state_pool_memory.h"
#include "susamune/state_live_video.hxx"
#include "susamune/state_restore_bindings.hxx"
static StateRestoreBindings::Words sRestoreBindings = {};
#define private public
#include "susamune/savestate.hxx"
#undef private
typedef long long OSTime;
extern "C" void*memcpy(void*d,const void*s,__SIZE_TYPE__ n){u8*a=(u8*)d;const u8*b=(const u8*)s;while(n--)*a++=*b++;return d;}
extern "C" void*memset(void*d,int c,__SIZE_TYPE__ n){u8*a=(u8*)d;while(n--)*a++=(u8)c;return d;}
extern "C" char*strncpy(char*d,const char*s,__SIZE_TYPE__ n){char*out=d;while(n--)*d++=*s?*s++:0;return out;}
enum{SETTING_SAVESTATE_FEEDBACK};
static bool confirmations;
struct Settings{bool getBool(int){return confirmations;}}gSettings;
struct Menu{enum{kToastFrames=120};char message[48];u32 calls;void toast(const char*s){strncpy(message,s,47);message[47]=0;++calls;}}menu;
static Menu*gMenu=&menu;
#define SET_STATUS(text) ((void)0)
static u32 policyCalls,practiceCalls;
static bool ownReplay;
static u8 take[16],ordinary[16];
static StateLiveVideo::Range sLiveVideo = {};
namespace PracticeSession{
bool savestateRestoreSpans(const SavestateData&data,StateCodec::WriteSpan(&out)[kSavestateSpanCount]){
 out[0]={take,0};out[1]={take,0};return data.version==1;}
bool copySavestateBytes(void*d,const void*,u32 n){++practiceCalls;return ownReplay&&d>=take&&(u8*)d+n<=take+sizeof(take);}
}
namespace StateArchiveProfile{void copyGameBytes(void*,void*d,const void*s,unsigned int n){++policyCalls;memcpy(d,s,n);}}
static u32 poolCapacity(){return 192;}
SavestateManager::SavestateManager(){}
'''

EXPORTS = r'''
static SavestateManager manager;
extern "C" {
__declspec(dllexport) void reset(){
 memset(&sPool,0,sizeof(sPool));memset(sSlots,0,sizeof(sSlots));memset(&menu,0,sizeof(menu));
 memset(&manager,0,sizeof(manager));memset(take,0x55,sizeof(take));memset(ordinary,0x55,sizeof(ordinary));
 confirmations=ownReplay=false;policyCalls=practiceCalls=0;sLiveVideo={0,0};
 for(u32 i=0;i<3;++i){sPool.slots[i]={i*64,64};StoredState&s=sSlots[i];
  s.header.magic=kSnapshotMagic;s.header.version=kSnapshotVersion;s.header.game_version=SUSAMUNE_GAME_VERSION;
  s.header.area_id=(u8)(i+2);s.header.episode_id=(u8)(i+3);s.generation=i+101;s.packedSize=64;
  s.practice.version=1;s.practice.frames=i+71;s.metadataTag=metadataTag(s);}
 sPool.used=192;
}
__declspec(dllexport) u32 slot(u32 i,u32 field){const auto s=manager.slotInfo(i);
 switch(field){case 0:return s.valid;case 1:return s.generation;case 2:return s.area;case 3:return s.episode;}return s.packedBytes;}
__declspec(dllexport) u32 practice(u32 i){PracticeSession::SavestateData data={};
 return manager.practiceData(i,&data)?data.frames:0;}
__declspec(dllexport) void damage(u32 i){sSlots[i].metadataTag^=1;}
__declspec(dllexport) u32 metadataSize(){return sizeof(StoredState);}
__declspec(dllexport) u32 headerSize(){return sizeof(SavestateHeader);}
__declspec(dllexport) void feedback(u32 enabled,u32 error){confirmations=enabled!=0;
 manager.feedback(error?"E:space":"saved",error?"State 3 won't fit - clear another slot":"State 3 saved");}
__declspec(dllexport) const char*message(){return menu.message;}
__declspec(dllexport) u32 value(u32 field){switch(field){case 0:return menu.calls;case 1:return manager.mFeedbackFrames;
 case 2:return policyCalls;case 3:return practiceCalls;}return 0;}
__declspec(dllexport) u32 copy(u32 replay,u32 durable,u32 isTake){
 const u8 input[16]={9};ownReplay=replay!=0;void*dest=isTake?take:ordinary;
 copyStateBytes(durable?(void*)1:0,dest,input,16);return *(u8*)dest;}
__declspec(dllexport) void protectVideo(u32 offset,u32 size){
 sLiveVideo={(__UINTPTR_TYPE__)(ordinary+offset),(__UINTPTR_TYPE__)(ordinary+offset+size)};}
__declspec(dllexport) u32 byte(u32 offset){return ordinary[offset];}
}
'''


class SavestateCheckpointContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / 'toolchain/clang++.exe'
        if not compiler.exists():
            raise unittest.SkipTest('Bundled host compiler required')
        cls.temp = tempfile.TemporaryDirectory(prefix='moonshine-state-contract-')
        cls.addClassCleanup(cls.temp.cleanup)
        production = SOURCE.read_text()
        stored = production[production.index('const u32 kSnapshotMagic'):production.index('StateSlotPool sPool;')]
        methods = ''.join(function_source(SOURCE, method) for method in (
            'u32 metadataTag(', 'bool validStore()', 'void copyBaseStateBytes(', 'void copyOwnedStateBytes(', 'void copyStateBytes(',
            'SavestateManager::SlotInfo SavestateManager::slotInfo(',
            'bool SavestateManager::practiceData(', 'void SavestateManager::feedback('))
        cls.libs = {}
        for region, count in (('JP', 11), ('US', 9), ('PAL', 9)):
            path = Path(cls.temp.name) / f'{region}.cpp'
            source = FIXTURE + f'const int kNumStaticRanges={count},kNumPointedAllocs=8;\n'
            source += stored + '\nStateSlotPool sPool; StoredState sSlots[3];bool sMetadataReady=true;\n' + methods + EXPORTS
            path.write_text(source)
            result = subprocess.run([str(compiler), '--target=x86_64-pc-windows-msvc', '-shared',
                '-nostdlib', '-fuse-ld=lld', '-Wl,/noentry', '-O2', '-fno-builtin', '-mno-stack-arg-probe',
                '-Wno-macro-redefined', f'-DSUSAMUNE_GAME_VERSION={len(cls.libs)+1}',
                '-I', str(ROOT/'include'), str(path), '-o', str(path.with_suffix('.dll'))], capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            lib = C.CDLL(str(path.with_suffix('.dll')))
            cls.addClassCleanup(lambda lib=lib:C.windll.kernel32.FreeLibrary(C.c_void_p(lib._handle)))
            lib.message.restype = C.c_char_p
            cls.libs[region] = lib

    def test_all_three_slots_publish_metadata_and_practice_on_every_region(self):
        for region, lib in self.libs.items():
            with self.subTest(region=region):
                lib.reset()
                self.assertEqual(lib.metadataSize(), 6960 if region == 'JP' else 6936)
                self.assertLessEqual(lib.metadataSize(), 7168)
                self.assertLessEqual(lib.headerSize(), 0x120)
                for slot in range(3):
                    self.assertEqual([lib.slot(slot, field) for field in range(5)],
                                     [1, slot+101, slot+2, slot+3, 64])
                    self.assertEqual(lib.practice(slot), slot+71)
                self.assertEqual(lib.practice(3), 0)
                lib.damage(2)
                for slot in range(3):
                    self.assertEqual(lib.slot(slot, 0), 0)
                    self.assertEqual(lib.practice(slot), 0)

    def test_failures_remain_visible_with_confirmations_off(self):
        lib = self.libs['JP']
        for enabled in (0, 1):
            lib.reset()
            lib.feedback(enabled, 1)
            self.assertEqual(lib.value(0), 1)
            self.assertEqual(lib.message(), b"State 3 won't fit - clear another slot")
            self.assertEqual(lib.value(1), 0)
            lib.reset()
            lib.feedback(enabled, 0)
            self.assertEqual(lib.value(0), 0)
            self.assertEqual(lib.value(1), 120 if enabled else 0)

    def test_replay_tape_copy_policy_precedes_optional_owner_filter(self):
        lib = self.libs['JP']
        for replay in (0, 1):
            for durable in (0, 1):
                for take in (0, 1):
                    with self.subTest(replay=replay, durable=durable, take=take):
                        lib.reset()
                        skipped = bool(replay and take)
                        self.assertEqual(lib.copy(replay, durable, take), 0x55 if skipped else 9)
                        self.assertEqual(lib.value(3), 1)
                        self.assertEqual(lib.value(2), int(durable and not skipped))

    def test_live_video_skip_keeps_owner_filter_on_both_remaining_sides(self):
        for region, lib in self.libs.items():
            for durable in (0, 1):
                with self.subTest(region=region, durable=durable):
                    lib.reset()
                    lib.protectVideo(3, 7)
                    lib.copy(0, durable, 0)
                    self.assertEqual([lib.byte(i) for i in range(16)],
                                     [9, 0, 0] + [0x55] * 7 + [0] * 6)
                    self.assertEqual(lib.value(2), 2 * durable)
                    self.assertEqual(lib.value(3), 1)

    def test_practice_sidecar_is_validated_before_restore_and_adopted_after_cleanup(self):
        save = function_source(SOURCE, 'bool SavestateManager::saveSlotExplicit(')
        self.assertLess(save.index('PracticeSession::captureSavestate('), save.index('compressCandidate('))
        load = function_source(SOURCE, 'bool SavestateManager::loadSlot(')
        self.assertLess(load.index('PracticeSession::savestateRestoreSpans('), load.index('OSDisableInterrupts()'))
        self.assertLess(load.index('PracticeSession::onSavestateLoaded()'), load.index('PracticeSession::restoreSavestate('))
        self.assertIn('PracticeSession::restoreSavestate(saved.practice, slot, saved.generation)', load)
        self.assertIn('const bool practiceRestored = PracticeSession::restoreSavestate(', load)
        self.assertIn('if (practiceRestored) feedback(', load)
        self.assertIn('SD read failed; state %lu restored, TAS stopped', load)
        for method in ('bool archiveCandidateMatches(', 'bool prepareSDRecovery('):
            self.assertIn('PracticeSession::savestateRestoreSpans(', function_source(SOURCE, method))


if __name__ == '__main__':
    unittest.main()
