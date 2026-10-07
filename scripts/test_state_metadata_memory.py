"""Fixed metadata admission must precede every access to the teaching-bank tail."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_practice_tape import function_source

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'src/savestate.cpp'


class StateMetadataMemoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='moonshine-state-metadata-')
        cls.addClassCleanup(cls.temp.cleanup)
        source=SOURCE.read_text()
        types=source[source.index('const u32 kSnapshotMagic'):source.index('StateSlotPool sPool;')]
        refs=source[source.index('StoredState (&sSlots)'):source.index('u32 sActiveSlot;')]
        fixture=r'''
#include "susamune/qft_timer.hxx"
#include "susamune/iling.hxx"
#include "susamune/ghost.hxx"
#include "susamune/practice_session.hxx"
#include "susamune/state_archive_profile.hxx"
#include "susamune/state_pool_memory.h"
#include "susamune/susamune_cfg.h"
#include "susamune/ghost_storage.h"
#include "susamune/savestate.hxx"
typedef long long OSTime;
extern "C" void*memset(void*d,int c,__SIZE_TYPE__ n){u8*a=(u8*)d;while(n--)*a++=(u8)c;return d;}
static const int kNumStaticRanges=11,kNumPointedAllocs=8;
static_assert(SUSAMUNE_CONSOLE_STATE_METADATA_PPC_BASE==0x91C11F00u,"console address");
static_assert(SUSAMUNE_DOLPHIN_STATE_METADATA_PPC_BASE==0x712D2F00u,"emulator address");
static_assert(SUSAMUNE_STATE_METADATA_RUNTIME_SIZE==53504u,"exact tail");
static_assert(SUSAMUNE_STATE_LIVE_PROFILE_OFFSET==0x7000u,"four maximum metadata records");
static_assert(SUSAMUNE_STATE_LIVE_PROFILE_SIZE==5920u,"exact profile size");
static_assert(SUSAMUNE_CONSOLE_STATE_METADATA_PPC_BASE+SUSAMUNE_STATE_LIVE_PROFILE_OFFSET==0x91C18F00u,"console profile address");
static_assert(SUSAMUNE_DOLPHIN_STATE_METADATA_PPC_BASE+SUSAMUNE_STATE_LIVE_PROFILE_OFFSET==0x712D9F00u,"emulator profile address");
static_assert(SUSAMUNE_STATE_METADATA_OFFSET==SUSAMUNE_GHOST_INPUT_MAX_COUNT*sizeof(SusamuneGhostInputSample),"full inputs preserved");
static_assert(SUSAMUNE_GHOST_V6_INPUT_MAX_COUNT*24u==SUSAMUNE_STATE_METADATA_OFFSET,"V6 payload uses the same bound");
static_assert(SUSAMUNE_PRESENTATION_PPC_BASE==(IS_EMULATOR ? 0x713B3000u : 0x91CF2000u),"physical playback tail remains distinct from metadata");
static_assert(SUSAMUNE_HSL_CACHE_PPC_BASE+257u*6u<=SUSAMUNE_GHOST_WATER_PPC_BASE,"display scratch slices cannot overlap");
static_assert(SUSAMUNE_GHOST_WATER_PPC_BASE+SUSAMUNE_GHOST_WATER_SIZE<=SUSAMUNE_PRESENTATION_PPC_BASE+SUSAMUNE_PRESENTATION_SIZE,"display scratch cannot reach file transfers");
static SusamuneCfg testCfg;
static SusamuneGhostStorageMailbox testMailbox;
alignas(32) static u8 memory[SUSAMUNE_STATE_METADATA_RUNTIME_SIZE+64];
static u32 invalidations;static bool wrongInvalidate;
static void DCInvalidateRange(void*p,u32 n){++invalidations;wrongInvalidate|=p!=&testMailbox.response||n!=32;}
#undef SUSAMUNE_CFG_PPC_PTR
#define SUSAMUNE_CFG_PPC_PTR (&testCfg)
#undef SUSAMUNE_GHOST_STORAGE_PPC_PTR
#define SUSAMUNE_GHOST_STORAGE_PPC_PTR (&testMailbox)
#undef SUSAMUNE_STATE_METADATA_PPC_BASE
#define SUSAMUNE_STATE_METADATA_PPC_BASE ((__UINTPTR_TYPE__)(memory+32))
static StateSlotPool sPool;
static u32 poolCapacity(){return 123456;}
SavestateManager::SavestateManager(){}
'''+types+refs
        for name in ('bool stateMetadataAvailable()', 'void initStateMetadata()',
                     'u32 metadataTag(', 'bool validStore()',
                     'SavestateManager::SlotInfo SavestateManager::slotInfo('):
            fixture+=function_source(SOURCE,name)+'\n'
        fixture+=r'''
static SavestateManager manager;
extern "C" {
__declspec(dllexport) void setup(u32 fault){
 memset(memory,0xa5,sizeof(memory));memset(&sPool,0,sizeof(sPool));
 testCfg.magic=SUSAMUNE_CFG_MAGIC;testCfg.version=SUSAMUNE_CFG_VERSION;
 testCfg.flags=SUSAMUNE_CFG_FLAG_STATE_POOL_EXPANSION|SUSAMUNE_CFG_FLAG_STATE_CODEC_RELOCATED;
 testMailbox.response.responseMagic=SUSAMUNE_GHOST_STORAGE_MAGIC;
 testMailbox.response.protocolVersion=SUSAMUNE_GHOST_STORAGE_VERSION;
 if(fault==1)testCfg.magic=0;if(fault==2)++testCfg.version;
 if(fault==3)testCfg.flags&=~SUSAMUNE_CFG_FLAG_STATE_POOL_EXPANSION;
 if(fault==4)testCfg.flags&=~SUSAMUNE_CFG_FLAG_STATE_CODEC_RELOCATED;
 if(fault==5)testMailbox.response.responseMagic=0;
 if(fault==6)--testMailbox.response.protocolVersion;
 if(fault==7)++testMailbox.response.protocolVersion;
 invalidations=0;wrongInvalidate=false;initStateMetadata();
}
__declspec(dllexport) u32 get(u32 key){switch(key){
 case 0:return sMetadataReady;case 1:return validStore();case 2:return invalidations;
 case 3:return wrongInvalidate;case 4:return sizeof(sSlots)+sizeof(sCandidate);
 case 5:return manager.slotInfo(0).valid;case 6:return manager.slotInfo(0).generation;
 case 7:return (__UINTPTR_TYPE__)&sLiveArchiveProfile-(__UINTPTR_TYPE__)(memory+32);
 case 8:return sizeof(sLiveArchiveProfile);}
 return 0;}
__declspec(dllexport) const void*bytes(){return memory;}
__declspec(dllexport) void fillProfile(u32 value){if(validStore())memset(&sLiveArchiveProfile,value,sizeof(sLiveArchiveProfile));}
__declspec(dllexport) void reinitializeMetadata(){initStateMetadata();}
__declspec(dllexport) void changeMailbox(){testCfg.magic=0;testMailbox.response.protocolVersion=0;}
}
'''
        cls.libs=[]
        for emulator in (0,1):
            path=Path(cls.temp.name)/f'memory{emulator}.cpp';path.write_text(fixture)
            proc=subprocess.run([str(ROOT/'toolchain/clang++.exe'),'--target=x86_64-pc-windows-msvc',
                '-shared','-O2','-fno-builtin','-mno-stack-arg-probe','-nostdlib','-fuse-ld=lld','-Wl,/noentry',
                f'-DIS_EMULATOR={emulator}','-DSUSAMUNE_GAME_VERSION=1','-I',str(ROOT/'include'),str(path),
                '-o',str(path.with_suffix('.dll'))],capture_output=True,text=True)
            if proc.returncode:raise AssertionError(proc.stdout+proc.stderr)
            lib=C.CDLL(str(path.with_suffix('.dll')));cls.libs.append(lib)
            lib.bytes.restype=C.c_void_p
            cls.addClassCleanup(lambda h=lib._handle:C.windll.kernel32.FreeLibrary(C.c_void_p(h)))

    def test_old_or_unknown_console_layout_never_reads_or_initializes_metadata(self):
        lib=self.libs[0]
        for fault in range(1,8):
            lib.setup(fault)
            lib.fillProfile(0x5a)
            self.assertEqual([lib.get(i)for i in (0,1,5,6)],[0,0,0,0])
            self.assertEqual(C.string_at(lib.bytes(),53568),bytes([0xa5])*53568)
            self.assertEqual(lib.get(2),int(fault>=5))
            self.assertEqual(lib.get(3),0)

    def test_current_console_and_dolphin_initialize_only_the_four_objects(self):
        for emulator,lib in enumerate(self.libs):
            for fault in (range(8) if emulator else (0,)):
                lib.setup(fault);used=lib.get(4)
                self.assertEqual([lib.get(i)for i in (0,1,5,6)],[1,1,0,0])
                self.assertLessEqual(used,53504)
                self.assertEqual(C.string_at(lib.bytes(),53568),
                    bytes([0xa5])*32+bytes(used)+bytes([0xa5])*(53536-used))
                self.assertEqual(lib.get(2),0 if emulator else 1)
                self.assertEqual(lib.get(3),0)
                lib.changeMailbox();self.assertEqual(lib.get(1),1)

    def test_live_profile_uses_only_its_reserved_tail_and_survives_metadata_reset(self):
        for lib in self.libs:
            lib.setup(0);used=lib.get(4);offset=lib.get(7);size=lib.get(8)
            self.assertEqual((offset,size),(0x7000,5920))
            self.assertLessEqual(used,offset)
            self.assertEqual(offset%32,0)
            lib.fillProfile(0x5a)
            expected=(bytes([0xa5])*32+bytes(used)+bytes([0xa5])*(offset-used)+
                      bytes([0x5a])*size+bytes([0xa5])*(53536-offset-size))
            self.assertEqual(C.string_at(lib.bytes(),53568),expected)
            lib.reinitializeMetadata()
            self.assertEqual(C.string_at(lib.bytes(),53568),expected)

    def test_public_paths_validate_before_access_and_constructor_only_uses_gated_initialization(self):
        init=function_source(SOURCE,'SavestateManager::SavestateManager()')
        self.assertIn('initStateMetadata()',init)
        self.assertNotIn('memset(sSlots',init)
        self.assertNotIn('sCandidate',init)
        store=function_source(SOURCE,'bool validStore()')
        self.assertLess(store.index('if (!sMetadataReady)'),store.index('sSlots[i]'))
        for name,access in (('saveSlotExplicit','memset(&sCandidate'),('loadSlot','StoredState &saved'),
                            ('clearSlot','sSlots[slot].header'),('beginSDExport','sSlots[slot].generation'),
                            ('beginSDLoad','sSlots[sDiskSlot].generation')):
            body=function_source(SOURCE,f'bool SavestateManager::{name}(')
            self.assertLess(body.index('validStore()'),body.index(access))
        self.assertIn('sMetadataReady && StateStorage::available()',
            function_source(SOURCE,'bool SavestateManager::sdAvailable()'))

    def test_live_profile_is_captured_after_admission_and_never_archived_as_live_state(self):
        source=SOURCE.read_text()
        self.assertNotIn('StateArchiveProfile::Data sLiveArchiveProfile;',source)
        load=function_source(SOURCE,'bool SavestateManager::loadSlot(')
        self.assertLess(load.index('validStore()'),load.index('captureArchiveProfile(sLiveArchiveProfile)'))
        self.assertLess(load.index('StateArchiveProfile::matches(saved.archiveProfile, sLiveArchiveProfile)'),
                        load.index('prepareSDRecovery('))
        update=function_source(SOURCE,'void SavestateManager::updateDisk(')
        self.assertLess(update.index('validStore()'),update.index('archiveCandidateMatches('))
        candidate=function_source(SOURCE,'bool archiveCandidateMatches(')
        self.assertLess(candidate.index('captureArchiveProfile(sLiveArchiveProfile)'),
                        candidate.index('StateArchiveProfile::matches(sCandidate.archiveProfile, sLiveArchiveProfile)'))
        save=function_source(SOURCE,'bool SavestateManager::saveSlotExplicit(')
        self.assertIn('captureArchiveProfile(sCandidate.archiveProfile)',save)
        self.assertNotIn('sLiveArchiveProfile',save)
        regions=function_source(SOURCE,'bool validSnapshotRegions(')
        self.assertIn('heapEnd > 0x81800000u',regions)
        self.assertIn('target >= 0x81800000u',regions)
        ghost=ROOT/'src/ghost.cpp'
        decode=function_source(ghost,'bool decodeSavedPrefix(')
        self.assertIn('track.inputCount > SusamuneGhostInputLimit(track.formatVersion)',decode)
        restore=function_source(ghost,'bool savestateRestoreSpans(')
        self.assertIn('saved.track.inputCount * SusamuneGhostInputStride(saved.track.formatVersion)',restore)


if __name__=='__main__':unittest.main()
