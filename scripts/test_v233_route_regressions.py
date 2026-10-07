"""Execute appended-route names, Plaza overlays and clean Full Reds finishes."""
import ctypes as C
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_iling_attempt_lifecycle import function

ROOT = Path(__file__).resolve().parents[1]


class RouteRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='moonshine-route-regressions-')
        cls.addClassCleanup(cls.temp.cleanup)
        source = (ROOT/'src/iling.cpp').read_text()
        sequence = (ROOT/'include/SMS/System/GameSequence.hxx').read_text()
        code = '#include "susamune/iling.hxx"\n#include "susamune/packed_text.hxx"\n'
        code += 'struct TGameSequence {' + sequence[sequence.index('    enum Area {'):sequence.index('    void set(')] + '};\n'
        code += 'namespace LevelWarp{struct Dest{enum{POST_CORONA=128};u8 area,episode,gameInt3;};}\n'
        code += '#define SUSAMUNE_ILING_PB_SLOT_COUNT 136\n'
        code += (ROOT/'src/packed_text.cpp').read_text()
        code += source[source.index('enum FinishKind {'):source.index('struct AttemptState {')]
        code += r'''
char sGeneratedLabel[kRegularLabelSize],sGeneratedShortLabel[6];
// This fixture calls only literal and Full Reds labels.
int sprintf(char*,const char*,...){return 0;}
extern "C" void *memset(void*p,int v,unsigned long long n){auto*out=(volatile unsigned char*)p;while(n--)*out++=(unsigned char)v;return p;}
int selectedEpisode(int entry){return kEntries[entry].start.gameInt3;}
OverlayFlag sOverlayFlags[kOverlayFlagCount];u8 sOverlayCount;
bool sCarryRestorePending,sHavePlazaStoryFlags,sHaveSetupMovieFlag,sHaveSetupShineCount;
u8 sPlazaStoryFlags,sSetupShineCount;bool sSetupMovieFlag;
LevelWarp::Dest sAttemptStart;
struct TFlagManager {
    static TFlagManager *smInstance;
    struct{u8 m1Type[0x80];}Type1Flag;
    s32 other[0x70000];
    s32 getFlag(u32 key){if((key>>16)==1){u32 bit=key&0xffff;return(Type1Flag.m1Type[bit>>3]>>(bit&7))&1;}return other[key];}
    bool getBool(u32 key){return getFlag(key);}
    void setFlag(u32 key,s32 value){if((key>>16)==1){u32 bit=key&0xffff;auto&b=Type1Flag.m1Type[bit>>3];b=(b&~(1<<(bit&7)))|((value!=0)<<(bit&7));}else other[key]=value;}
    void setBool(bool value,u32 key){setFlag(key,value);}
}flags;
TFlagManager *TFlagManager::smInstance=&flags;
'''
        for name in ('validEntry','isPlazaEntry','pbSlot','readOverlayFlag','writeOverlayFlag',
                     'applyOverlayFlag','restoreOverlayFlags','reapplyOverlayFlags',
                     'plazaStoryProfile','applyPlazaOverlay','plazaOverlayRunsLive',
                     'restorePlazaStoryFlags','restorePlazaSetupState','label','shortLabel'):
            code += function(source,name)
        code += r'''
#define API extern "C" __declspec(dllexport)
API const char* name(int entry){return label(entry);}
API const char* shortName(int entry){return shortLabel(entry);}
API void setup(int scenario,int unlocked,int shine){
    for(auto &v:flags.Type1Flag.m1Type)v=0;
    for(auto &v:flags.other)v=0;
    sOverlayCount=0;sHavePlazaStoryFlags=sHaveSetupMovieFlag=sHaveSetupShineCount=false;
    flags.setBool(unlocked,0x1038f);flags.setFlag(0x10021,shine);
    sAttemptStart={1,(u8)scenario,9};applyPlazaOverlay(132);
    // Match the production post-setup restoration boundary.
    restorePlazaSetupState();reapplyOverlayFlags();
    if(!plazaOverlayRunsLive(132)){restoreOverlayFlags(true);restorePlazaStoryFlags();}
}
API int value(int key){return flags.getFlag(key);}
API void leave(){restoreOverlayFlags(true);restorePlazaStoryFlags();}
API int live(int scenario){sAttemptStart.episode=(u8)scenario;return plazaOverlayRunsLive(132);}
'''
        path = Path(cls.temp.name)/'routes.cpp';path.write_text(code)
        proc = subprocess.run([str(ROOT/'toolchain/clang++.exe'),'--target=x86_64-pc-windows-msvc',
            '-shared','-nostdlib','-fuse-ld=lld','-Wl,/noentry','-O2',
            '-I',str(ROOT/'include'),'-I',str(ROOT/'src'),str(path),'-o',str(path.with_suffix('.dll'))],
            capture_output=True,text=True)
        if proc.returncode:raise RuntimeError(proc.stdout+proc.stderr)
        cls.lib=C.CDLL(str(path.with_suffix('.dll')))
        cls.lib.name.restype=cls.lib.shortName.restype=C.c_char_p
        cls.addClassCleanup(lambda:C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def test_every_literal_and_full_reds_label_matches_its_catalogue_identity(self):
        entries=(ROOT/'src/iling_entries.inc').read_text()
        names=re.findall(r'^\w+\("([^"]+)"',entries,re.M)
        self.assertEqual(len(names),133)
        for entry in range(90,133):
            self.assertEqual(self.lib.name(entry).decode(),names[entry],entry)
        self.assertEqual(self.lib.shortName(130),b'NB6FR')
        self.assertEqual(self.lib.shortName(131),b'PV5FR')
        self.assertEqual(self.lib.shortName(132),b'GE')

    def test_yoshi_story_stays_available_through_intro_then_restores_original_save_flags(self):
        for unlocked in (0,1):
            for shine in (0,1):
                self.lib.setup(8,unlocked,shine)
                self.assertEqual((self.lib.value(0x1038f),self.lib.value(0x10021)),(0,1))
                self.lib.leave()
                self.assertEqual((self.lib.value(0x1038f),self.lib.value(0x10021)),(unlocked,shine))

    def test_only_live_story_scenarios_retain_the_temporary_story_flags(self):
        self.assertEqual([i for i in range(10) if self.lib.live(i)],[0,1,5,7,8])


if __name__=='__main__':unittest.main()
