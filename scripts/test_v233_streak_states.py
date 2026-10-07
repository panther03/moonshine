"""Execute production streak transitions with timer and result sinks stubbed."""
import ctypes as C
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from test_native_timer_creation import function
from test_practice_tape import function_source

ROOT = Path(__file__).resolve().parents[1]


class StreakStateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='moonshine-streak-state-')
        cls.addClassCleanup(cls.temp.cleanup)
        source = (ROOT/'src/stage_loader.cpp').read_text()
        menu = (ROOT/'src/menu.cpp').read_text()
        code = '#include "susamune/stage_loader.hxx"\nenum SettingId {SETTING_STREAK_AUTO_RESET};\n'
        for enum in ('SessionState', 'Outcome', 'ModalState'):
            code += re.search(r'enum '+enum+r' \{.*?\n};', source, re.S).group(0)
        code += '\nconst int kQueueActionBytes=15;\n'
        code += re.search(r'struct StageLoaderRuntime \{.*?\n};', source, re.S).group(0)
        code += r'''
StageLoaderRuntime sRuntime;
struct Timer {u32 serial;u32 attemptSerial(){return serial;}}gQFTTimer;
struct Mario {u32 mState;}mario,*gpMarioOriginal=&mario;
const u32 kMarioWinDemoState=0x1302;
const int kRetryDelayFrames=6;
bool resetOn=true;int failures,successes,observations,invalidations;
struct Options {bool getBool(SettingId){return resetOn;}}gSettings;
void clearShinePublishLatch(){sRuntime.shinePublishLatchFrames=0;}
int expectedStartEntry(){return 2;}int expectedResultEntry(){return 2;}
s32 liveQf(){return 100;}
void observeQf(s32){++observations;}
void incrementSaturated(u32&v){if(v!=0xffffffffu)++v;}
void addSaturated(u64&v,u32 n){v+=n;}
void queueFailure(Outcome outcome,s32){++failures;sRuntime.currentStreak=0;sRuntime.outcome=outcome;sRuntime.state=STATE_RETRY_DELAY;}
void queueSuccess(int,s32){++successes;++sRuntime.currentStreak;}
namespace ILing {bool sameEpisodeShine(int,int){return false;}u8 rejectionCause(){return 3;}}
namespace StageLoader {void invalidatePlaylistBest(){++invalidations;}}
'''
        code += function(source, 'beginAttempt')
        code += '\nnamespace StageLoader {\n'
        for name in ('active', 'onILAttemptStarted', 'onILAttemptEnded', 'onSavestateLoaded', 'onILResult'):
            code += function(source, name)
        code += '}\n'
        code += function_source(ROOT/'src/menu.cpp', 'static bool parseTarget(')
        code += r'''
#define API extern "C" __declspec(dllexport)
API void reset(){sRuntime={};sRuntime.mode=StageLoader::MODE_STREAKING;sRuntime.state=STATE_RUNNING;
 sRuntime.currentStreak=5;sRuntime.progress=5;sRuntime.attemptSerial=1;sRuntime.targetQf=200;
 gQFTTimer.serial=1;mario.mState=0;resetOn=true;failures=successes=observations=invalidations=0;}
API int load(){return StageLoader::onSavestateLoaded();}
API void finish(int entry,int eligible){StageLoader::onILResult(entry,100,eligible);}
API void start(int entry,int continuation){++gQFTTimer.serial;StageLoader::onILAttemptStarted(entry,continuation);}
API void end(){StageLoader::onILAttemptEnded();}
API void state(int v){sRuntime.state=v;}
API void loader(){sRuntime.mode=StageLoader::MODE_LOADER;}
API void savebox(){resetOn=false;mario.mState=kMarioWinDemoState;}
API int get(int field){switch(field){case 0:return sRuntime.currentStreak;case 1:return sRuntime.practiceLoaded;
 case 2:return sRuntime.state;case 3:return failures;case 4:return successes;case 5:return sRuntime.eligibleCompletes;
 case 6:return sRuntime.progress;case 8:return sRuntime.rejectionCause;default:return observations;}}
API int parse(const char*text){s32 result=-9;return parseTarget(text,&result)?result:-2;}
'''
        path = Path(cls.temp.name)/'streak.cpp'
        path.write_text(code)
        proc = subprocess.run([str(ROOT/'toolchain/clang++.exe'), '--target=x86_64-pc-windows-msvc',
            '-shared','-nostdlib','-fuse-ld=lld','-Wl,/noentry','-O2','-I',str(ROOT/'include'),
            str(path),'-o',str(path.with_suffix('.dll'))], capture_output=True, text=True)
        if proc.returncode: raise RuntimeError(proc.stderr)
        cls.lib=C.CDLL(str(path.with_suffix('.dll')))
        cls.addClassCleanup(lambda:C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def setUp(self): self.lib.reset()

    def test_loaded_finishes_neither_add_nor_remove_streak(self):
        for entry,eligible in ((2,1),(2,0),(99,1)):
            self.lib.reset();self.assertEqual(self.lib.load(),1)
            self.lib.finish(entry,eligible)
            self.assertEqual([self.lib.get(i) for i in (0,3,4,5,6)], [5,0,0,0,5])
            self.assertEqual(self.lib.get(2),4) # retry delay

    def test_true_restart_resumes_normal_counting(self):
        self.lib.load();self.lib.start(2,0);self.lib.finish(2,1)
        self.assertEqual([self.lib.get(i) for i in (0,1,3,4,5)], [6,0,0,1,1])

    def test_death_retry_inside_loaded_full_route_remains_practice(self):
        self.lib.load();self.lib.start(2,1);self.lib.finish(2,1)
        self.assertEqual([self.lib.get(i) for i in (0,1,3,4)], [5,1,0,0])

    def test_loaded_attempt_end_and_wrong_route_retry_without_failure(self):
        self.lib.load();self.lib.end()
        self.assertEqual([self.lib.get(i) for i in (0,2,3)], [5,5,0])
        self.lib.reset();self.lib.load();self.lib.start(99,0)
        self.assertEqual([self.lib.get(i) for i in (0,2,3)], [5,5,0])

    def test_loaded_finish_honours_wait_for_savebox(self):
        self.lib.load();self.lib.savebox();self.lib.finish(2,1)
        self.assertEqual([self.lib.get(i) for i in (0,2,4)], [5,6,0])

    def test_inactive_complete_blocked_and_loader_are_not_reopened(self):
        for state in (0,8,9):
            self.lib.reset();self.lib.state(state)
            self.assertEqual(self.lib.load(),0)
            self.assertEqual(self.lib.get(2),state)
        self.lib.reset();self.lib.loader();self.assertEqual(self.lib.load(),0)

    def test_normal_failure_still_breaks_streak(self):
        self.lib.finish(2,0)
        self.assertEqual([self.lib.get(i) for i in (0,3)], [0,1])
        self.assertEqual(self.lib.get(8),3)

    def test_target_parser_matches_exact_conversion_and_rejects_overflow(self):
        self.assertEqual(self.lib.parse(b''),-1)
        for ms in (0,1,999,1000,1001,19999,20000,59999,60000,123456,9999999,17895695):
            text=f'{ms//60000}:{ms//1000%60:02d}.{ms%1000:03d}'.encode()
            qf=((ms+1)*120-1)//1001
            self.assertEqual(self.lib.parse(text),qf if qf<=0x7fffffff//1001 else -2)
        for text in (b'999999999999999',b'1:999999999',b'999:59.999',b'-1',b'1.1234',b'1:',b'1.'):
            self.assertEqual(self.lib.parse(text),-2,text)

if __name__=='__main__':unittest.main()
