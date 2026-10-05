"""Exercise real IL attempt initialization without inheriting the departing scene."""
import ctypes as C
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

def function(source, name):
    match = re.search(r"(?:^|\n)[^\n]*\b" + re.escape(name) + r"\([^;]*?\)\s*(?:const\s*)?\{", source)
    if not match:
        raise AssertionError(name)
    start = source.index("{", match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]

class AttemptLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='moonshine-il-lifecycle-')
        cls.addClassCleanup(cls.temp.cleanup)
        source=(ROOT/'src/iling.cpp').read_text()
        sequence=(ROOT/'include/SMS/System/GameSequence.hxx').read_text()
        code='typedef unsigned char u8;typedef unsigned short u16;typedef unsigned int u32;typedef int s32;\n'
        code+='struct TGameSequence {'+sequence[sequence.index('    enum Area {'):sequence.index('    void set(')]+'u8 mAreaID,mEpisodeID;u16 mFlags;};\n'
        code+='namespace LevelWarp {struct Dest{u8 area,episode,gameInt3;};u8 parentArea(u8 a){return a==0x20?4:a==0x3a?13:255;}}\n'
        code+=source[source.index('enum FinishKind {'):source.index('const int kSecretOnlyPbSlotFirst')]
        code+=function(source,'isSecretOnlyPbSlot').replace('constexpr ', '')
        code=code.replace('constexpr bool isSecretOnlyPbSlot', 'bool isSecretOnlyPbSlot')
        code=code.replace('return slot >= kSecretOnlyPbSlotFirst &&','return slot >= 70 &&').replace('slot <= kSecretOnlyPbSlotLast;', 'slot <= 79;')
        for name in ('kEntryPinnaEyg','kEntryPinna8','kEntrySirena8'):
            code+=re.search(r'const int '+name+r' = \d+;',source).group(0)+'\n'
        code+='''
struct Application {TGameSequence mCurrentScene,mPrevScene;}gpApplication;
struct Timer {u32 serial=1;u32 attemptSerial(){return serial;}}gQFTTimer;
struct TFlagManager {static TFlagManager *smInstance;void setFlag(u32,s32){}};
TFlagManager *TFlagManager::smInstance=0;
bool sPinnaEygRestart,sRunning,sAttemptReady,sAwaitingStageSetup,sTransitionPending;
bool sChildRetryContinuation,sSecretOnly,sRecordsEligible,sNativeIgt;
bool sBowserNozzleShieldActive,sBowserNozzleShieldPending;
s32 sSavedBowserNozzleFlag;LevelWarp::Dest sAttemptStart;
u8 sFinishKind,sAssistReasons;int sSelectedEntry;u32 sAttemptSerial;
u8 liveReasons;int recordStarts,recordInvalid,playlistInvalid,splitInvalid;
namespace Records {void onILAttemptStarted(int){++recordStarts;recordInvalid=0;}void invalidateAttempt(u8){++recordInvalid;}}
namespace StageLoader {void invalidatePlaylistBest(){++playlistInvalid;}bool fastAnyStart(int){return false;}}
namespace Assist {enum {OTHER=1};}
namespace SplitStats {void invalidateAttempt(){++splitInvalid;}}
u8 liveGlobalAssistReasons(){return liveReasons;}
bool isPlazaEntry(int){return false;}
void applyPlazaOverlay(int){}void applyEntryOverlay(int){}
int entryForChildMode(const TGameSequence&,int){return 26;}
void clearAttempt(){sRunning=false;sAwaitingStageSetup=false;sSelectedEntry=-1;}
'''
        for name in ('validEntry','pbSlot','sameDest','sessionStartChanged','sceneMatches','entryFinish','acceptsAnySelectedOrigin',
                     'isPinnaOneRouteScene','isPinnaEightReturn','acceptsSelectedOriginScene',
                     'isInternalScene','entryForStartScene','armAttempt','beginAttemptScene',
                     'beforeStageSetup','invalidateForAssist'):
            code+=function(source,name)
        code+='''
#define API extern "C" __declspec(dllexport)
API void reset(){liveReasons=0;sRunning=sAttemptReady=sAwaitingStageSetup=false;sSelectedEntry=-1;recordStarts=recordInvalid=playlistInvalid=splitInvalid=0;gpApplication.mCurrentScene={2,0,0};gpApplication.mPrevScene={2,0,0};}
API void reasons(int v){liveReasons=v;}
API void select(int i){armAttempt(kEntries[i],i);}
API void invalidate(int v){invalidateForAssist(v);}
API void setup(int a,int e){gpApplication.mPrevScene=gpApplication.mCurrentScene;gpApplication.mCurrentScene={(u8)a,(u8)e,0};beforeStageSetup();}
API int eligible(){return sRecordsEligible;}
API int assist(){return sAssistReasons;}
API int waiting(){return sAwaitingStageSetup;}
API int starts(){return recordStarts;}
API int rejected(){return recordInvalid;}
API int playlist(){return playlistInvalid;}
API int splits(){return splitInvalid;}
API void ready(){sAttemptReady=true;}
'''
        path=Path(cls.temp.name)/'lifecycle.cpp';path.write_text(code)
        proc=subprocess.run([str(ROOT/'toolchain/clang++.exe'),'--target=x86_64-pc-windows-msvc','-shared','-nostdlib','-fuse-ld=lld','-Wl,/noentry','-O2','-I',str(ROOT/'src'),str(path),'-o',str(path.with_suffix('.dll'))],capture_output=True,text=True)
        if proc.returncode:raise RuntimeError(proc.stdout+proc.stderr)
        cls.lib=C.CDLL(str(path.with_suffix('.dll')))
        cls.addClassCleanup(lambda:C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def setUp(self):self.lib.reset()

    def test_selected_fresh_stage_does_not_inherit_departing_assists(self):
        for entry,area,episode in ((26,0x20,0),(25,4,0),(36,4,6),(32,4,4)):
            self.lib.reset();self.lib.reasons(1);self.lib.select(entry);self.lib.invalidate(8)
            self.assertEqual(self.lib.waiting(),1)
            self.assertEqual(self.lib.starts(),0)
            self.assertEqual(self.lib.playlist(),0)
            self.lib.reasons(0);self.lib.setup(area,episode)
            self.assertEqual((self.lib.waiting(),self.lib.eligible(),self.lib.assist(),self.lib.starts()),(0,1,0,1))

    def test_new_stage_assist_settings_and_intro_actions_still_reject(self):
        self.lib.select(26);self.lib.reasons(1);self.lib.setup(0x20,0)
        self.assertEqual((self.lib.eligible(),self.lib.assist(),self.lib.rejected(),self.lib.playlist()),(0,1,1,1))
        self.lib.reset();self.lib.select(26);self.lib.setup(0x20,0);self.lib.invalidate(8)
        self.assertEqual((self.lib.eligible(),self.lib.assist(),self.lib.splits()),(0,8,1))

    def test_full_route_child_carries_real_assistance(self):
        self.lib.select(25);self.lib.setup(4,0);self.lib.ready();self.lib.invalidate(8)
        self.lib.setup(0x20,0)
        self.assertEqual((self.lib.eligible(),self.lib.assist(),self.lib.starts()),(0,8,1))

    def test_selected_restart_resets_old_eligibility_at_new_origin(self):
        self.lib.select(26);self.lib.setup(0x20,0);self.lib.ready();self.lib.invalidate(1)
        self.lib.setup(0x20,0)
        self.assertEqual((self.lib.eligible(),self.lib.assist(),self.lib.starts()),(1,0,2))

    def test_pinna_movie_carry_preserves_eligibility_and_starts_once(self):
        self.lib.select(38);self.lib.setup(13,0);self.lib.ready();self.lib.invalidate(1)
        self.lib.setup(13,6);self.lib.setup(0x3a,1);self.lib.setup(13,7)
        self.assertEqual((self.lib.eligible(),self.lib.assist(),self.lib.starts()),(0,1,1))

    def test_natural_entry_initializes_at_setup(self):
        self.lib.setup(4,4)
        self.assertEqual((self.lib.waiting(),self.lib.eligible(),self.lib.starts()),(0,1,1))

    def test_compact_streak_rejections_are_visible_but_normal_counter_stays_compact(self):
        source=(ROOT/'src/stage_loader.cpp').read_text();body=function(source,'draw')
        compact=body[body.index('if (display == 1)'):body.index('} else if (display == 0')]
        self.assertIn('drawCounter(menu);',compact)
        self.assertIn('sRuntime.mode == MODE_STREAKING',compact)
        self.assertIn('sRuntime.displayFrames > 0',compact)
        self.assertIn('sRuntime.outcome >= OUTCOME_RESET',compact)
        self.assertIn('drawFullNotice(menu);',compact)

if __name__=='__main__':unittest.main()
