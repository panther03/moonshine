"""Exercise real bind edges and state dispatch through the production modal gate."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_practice_tape import function_source

ROOT = Path(__file__).resolve().parents[1]


class HeldStateBindTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="moonshine-held-state-")
        cls.addClassCleanup(cls.temp.cleanup)
        source = r'''
#define private public
#include "susamune/binds.hxx"
#undef private
static Binds localBinds;
Binds &gBinds=localBinds;
enum {SETTING_ATTEMPT_COUNTER, SETTING_ATTEMPT_IN_STAGE_CONTROLS};
struct Settings {bool counter; bool getBool(int){return counter;}} gSettings;
static u32 saves,loads,cycles,holdMask;
static bool sAwaitingLoadApproval;
static u32 sLoadSlot,sPendingSlot,sPendingGeneration,sSelectedSD,sPendingSD;
namespace PracticeSession {void armLoadHold(u32 m){holdMask=m;}void cancelLoadHold(){holdMask=0;}}
namespace WarpWheel {
bool prompt,approved;
bool takeSavestateLoadApproval(){bool a=approved;approved=false;return a;}
bool promptPending(){return prompt;}
bool requestSavestateLoad(){return !prompt;}
}
#define SET_STATUS(x) ((void)0)
class SavestateManager {
public:
 bool mLoadPending;u32 mLoadWaitFrames;
 bool diskBusy(){return false;}
 struct Info{u32 generation;};Info slotInfo(u32){return {123};}
 void saveState(){++saves;}
 void cycleSlot(){++cycles;}void cycleSaveSlot(){++cycles;}void cycleLoadSlot(){++cycles;}
 void updateHook();
} manager,*gSavestateMgr=&manager;
'''
        source += function_source(ROOT/'src/binds.cpp', 'bool Binds::wasPressed(')
        source += function_source(ROOT/'src/savestate.cpp', 'void SavestateManager::updateHook()')
        source += r'''
#define API extern "C" __declspec(dllexport)
API void reset(){
 saves=loads=cycles=holdMask=0;sAwaitingLoadApproval=false;
 manager.mLoadPending=false;manager.mLoadWaitFrames=0;
 sLoadSlot=sPendingSlot=sPendingGeneration=sSelectedSD=sPendingSD=0;
 WarpWheel::prompt=WarpWheel::approved=gSettings.counter=false;
 localBinds.mHeld=localBinds.mPrevHeld=0;localBinds.mRecState=0;
 localBinds.mRecSilent=true; // e.g. the preceding Step, while A remains held
 for(unsigned i=0;i<BIND_COUNT;++i)localBinds.mMask[i]=0;
 localBinds.mMask[BIND_SAVESTATE_SAVE]=1;
 localBinds.mMask[BIND_SAVESTATE_LOAD]=2;
 localBinds.mMask[BIND_FAST_FORWARD_4X]=0x201;
 localBinds.mMask[BIND_FAST_FORWARD_8X]=0x202;
}
API void frame(unsigned buttons,unsigned blockers,unsigned recording){
 localBinds.mPrevHeld=localBinds.mHeld;localBinds.mHeld=(u16)buttons;
 localBinds.mRecState=(u8)recording;
 const bool observerFrame=blockers&1,creationEditing=blockers&2,
     sessionOwnsInput=blockers&4,stateDiskBusy=blockers&8,
     practiceModal=blockers&16,practiceStepConsumed=blockers&32;
'''
        source += function_source(ROOT/'src/main.cpp',
                                  'if (gSavestateMgr && !observerFrame && !creationEditing')
        source += r'''
 if(manager.mLoadPending){++loads;manager.mLoadPending=false;}
}
API unsigned get(unsigned n){switch(n){case 0:return saves;case 1:return loads;
 case 2:return cycles;case 3:return holdMask;case 4:return localBinds.mHeld;
 case 5:return gBinds.wasPressed(BIND_FAST_FORWARD_4X)||gBinds.wasPressed(BIND_FAST_FORWARD_8X);
 case 6:return sPendingGeneration;}return 0;}
API void bind(unsigned save,unsigned load){localBinds.mMask[BIND_SAVESTATE_SAVE]=(u16)save;
 localBinds.mMask[BIND_SAVESTATE_LOAD]=(u16)load;}
API void counter(unsigned on){gSettings.counter=on;
 localBinds.mMask[BIND_ATTEMPT_SHOW]=1;localBinds.mMask[BIND_ATTEMPT_ADD]=2;}
'''
        path = Path(cls.temp.name)/'held.cpp'
        path.write_text(source, encoding='ascii')
        result = subprocess.run([
            str(ROOT/'toolchain/clang++.exe'), '--target=x86_64-pc-windows-msvc',
            '-shared', '-nostdlib', '-fuse-ld=lld', '-Wl,/noentry', '-O2',
            '-I', str(ROOT/'include'), str(path), '-o', str(path.with_suffix('.dll'))
        ], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout+result.stderr)
        cls.lib=C.CDLL(str(path.with_suffix('.dll')))
        cls.addClassCleanup(lambda:C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def setUp(self):
        self.lib.reset()

    def test_save_and_load_accept_gameplay_buttons_and_triggers(self):
        for held in (0, 0x100, 0x200, 0x300, 0x360, 0xF70):
            with self.subTest(held=held):
                self.lib.reset()
                self.lib.frame(held, 0, 0)
                self.lib.frame(held|1, 0, 0)
                self.assertEqual((self.lib.get(0),self.lib.get(4)),(1,held|1))
                self.lib.frame(held, 0, 0)
                self.lib.frame(held|2, 0, 0)
                self.assertEqual((self.lib.get(1),self.lib.get(3),self.lib.get(6)),(1,2,123))
                self.assertEqual(self.lib.get(4),held|2)

    def test_held_shortcut_does_not_repeat_or_fire_on_other_button_edges(self):
        self.lib.frame(0x301,0,0)
        for held in (0x301,0x101,0x361,0x201,1):
            self.lib.frame(held,0,0)
        self.assertEqual(self.lib.get(0),1)
        self.lib.frame(0x300,0,0)
        self.lib.frame(0x301,0,0)
        self.assertEqual(self.lib.get(0),2)

    def test_step_silence_does_not_require_releasing_mario_buttons(self):
        self.lib.frame(0x308,32,0)
        self.lib.frame(0x300,0,0)
        self.lib.frame(0x302,0,0)
        self.assertEqual(self.lib.get(1),1)

    def test_every_modal_owner_and_step_commit_block_state_shortcuts(self):
        for block in (1,2,4,8,16,32):
            with self.subTest(block=block):
                self.lib.reset()
                self.lib.frame(0x301,block,0)
                self.lib.frame(0x301,0,0)
                self.assertEqual(self.lib.get(0),0)
                self.lib.frame(0x300,0,0)
                self.lib.frame(0x301,0,0)
                self.assertEqual(self.lib.get(0),1)

    def test_bind_recording_and_commit_do_not_save(self):
        for recording in (1,2):
            self.lib.reset()
            self.lib.frame(0x301,0,recording)
            self.lib.frame(0x300,16,0)
            self.assertEqual(self.lib.get(0),0)

    def test_state_shortcut_silences_overlapping_fast_forward(self):
        for buttons,index in ((0x201,0),(0x202,1)):
            self.lib.reset()
            self.lib.frame(buttons,0,0)
            self.assertEqual(self.lib.get(index),1)
            self.assertEqual(self.lib.get(5),0)

    def test_unassigned_and_incomplete_chords_remain_inert(self):
        self.lib.bind(0,0)
        self.lib.frame(0x303,0,0)
        self.assertEqual((self.lib.get(0),self.lib.get(1)),(0,0))
        self.lib.reset();self.lib.bind(0x41,0x42)
        self.lib.frame(0x301,0,0)
        self.assertEqual(self.lib.get(0),0)
        self.lib.frame(0x341,0,0)
        self.assertEqual(self.lib.get(0),1)

    def test_enabled_attempt_counter_keeps_its_explicit_collision_priority(self):
        self.lib.counter(1)
        self.lib.frame(0x301,0,0)
        self.lib.frame(0x302,0,0)
        self.assertEqual((self.lib.get(0),self.lib.get(1)),(0,0))


if __name__=='__main__':
    unittest.main()
