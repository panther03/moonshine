"""Execute queued warps through native pause/save state lifecycle boundaries."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_iling_attempt_lifecycle import function

ROOT = Path(__file__).resolve().parents[1]


class PausedWarpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='moonshine-paused-warp-')
        cls.addClassCleanup(cls.temp.cleanup)
        source = (ROOT/'src/warp_wheel.cpp').read_text()
        code = r'''
using u8=unsigned char;
int events, moves; bool ready, pending, busy;
void event(int value){events=events*10+value;}
struct TPauseMenu2{void setDrawEnd(){event(2);}}pause;
struct TMarDirector {
    enum{STATE_NORMAL=4,STATE_PAUSE_MENU=5,STATE_STAGE_EXIT=9,STATE_SAVE_CARD=11};
    u8 mCurState;int _260;TPauseMenu2 *mPauseMenu;
    void moveStage(){event(1);++moves;}
    void currentStateFinalize(u8 n){if(n==9)event(3);}
    void nextStateInitialize(u8 n){if(n==9)event(4);}
}director;
struct Card{int getLastStatus(){return busy?-1:0;}}card;
Card *gpCardManager=&card;constexpr int CARD_ERROR_BUSY=-1;
bool sArmed,sQueuedSessionDeathRestart,sWaitForRetailDeathTail,sWaitForSave;
int sSaveIdleFrames;
namespace StageLoader {bool departureResultPending(){return pending;}}
bool saveFlowActive(){return busy;}
bool armedCourseWarpReady(){return ready;}
void prepareArmedDeparture(){sArmed=false;}
namespace LevelWarp {
'''+function(source,'kick')+'}\n'+function(source,'servicePausedWarp')+r'''
#define API extern "C" __declspec(dllexport)
API void reset(int state){
    director={(u8)state,1,&pause};events=moves=0;
    ready=sArmed=true;pending=busy=sWaitForSave=false;sSaveIdleFrames=0;
    sQueuedSessionDeathRestart=sWaitForRetailDeathTail=false;
}
API void set(int option,int value){
    switch(option){case 0:sArmed=value;break;case 1:director._260=value;break;
    case 2:busy=value;break;case 3:ready=value;break;case 4:pending=value;break;
    case 5:sWaitForSave=value;break;case 6:sQueuedSessionDeathRestart=value;break;
    case 7:sWaitForRetailDeathTail=value;break;case 8:director.mPauseMenu=value?&pause:nullptr;}
}
API int tick(){return servicePausedWarp(&director);}
API int value(int id){return id==0?director.mCurState:id==1?events:id==2?moves:sArmed;}
API int nullDirector(){return servicePausedWarp(nullptr);}
'''
        path = Path(cls.temp.name)/'paused.cpp';path.write_text(code)
        result=subprocess.run([str(ROOT/'toolchain/clang++.exe'),
            '--target=x86_64-pc-windows-msvc','-shared','-nostdlib','-fuse-ld=lld',
            '-Wl,/noentry','-O2',str(path),'-o',str(path.with_suffix('.dll'))],
            capture_output=True,text=True)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        cls.lib=C.CDLL(str(path.with_suffix('.dll')))
        cls.addClassCleanup(lambda:C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def test_pause_closes_retail_ui_and_transitions_once(self):
        self.lib.reset(5)
        self.assertEqual(self.lib.tick(),1)
        self.assertEqual([self.lib.value(i)for i in range(4)],[9,1234,1,0])
        self.assertEqual(self.lib.tick(),0)
        self.assertEqual(self.lib.value(2),1)

    def test_save_dialog_runs_retail_lifecycle_without_pause_audio(self):
        self.lib.reset(11)
        self.assertEqual(self.lib.tick(),1)
        self.assertEqual([self.lib.value(i)for i in range(4)],[9,134,1,0])

    def test_busy_card_result_and_pb_decision_keep_departure_armed(self):
        for state in (5,11):
            for option,blocked,allowed in ((2,1,0),(3,0,1),(4,1,0),(6,1,0),(7,1,0)):
                self.lib.reset(state);self.lib.set(option,blocked)
                self.assertEqual(self.lib.tick(),0,(state,option))
                self.assertEqual([self.lib.value(i)for i in range(4)],[state,0,0,1])
                self.lib.set(option,allowed)
                self.assertEqual(self.lib.tick(),1,(state,option))

    def test_save_idle_guard_remains_in_force(self):
        self.lib.reset(11);self.lib.set(5,1);self.lib.set(2,1)
        self.assertEqual(self.lib.tick(),0)
        self.lib.set(2,0)
        self.assertEqual(self.lib.tick(),0)
        self.assertEqual(self.lib.tick(),1)

    def test_unready_normal_borrowed_and_unarmed_states_are_untouched(self):
        for state in (0,1,4,6,9,12):
            self.lib.reset(state)
            self.assertEqual(self.lib.tick(),0,state)
            self.assertEqual([self.lib.value(i)for i in range(3)],[state,0,0])
        for option in (0,1):
            self.lib.reset(5);self.lib.set(option,0)
            self.assertEqual(self.lib.tick(),0)
            self.assertEqual(self.lib.value(1),0)
        self.assertEqual(self.lib.nullDirector(),0)

    def test_service_runs_before_native_state_early_return(self):
        source=(ROOT/'src/warp_wheel.cpp').read_text()
        update=function(source[source.index('void update(TMarioGamePad *pad)'):],'update')
        self.assertLess(update.index('servicePausedWarp(gpMarDirector)'),
                        update.index('state != TMarDirector::STATE_NORMAL'))
        self.assertLess(update.index('sPrompt.action != PROMPT_NONE'),
                        update.index('servicePausedWarp(gpMarDirector)'))


if __name__=='__main__':unittest.main()
