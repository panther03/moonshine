"""Run the application-entry reset guard with retail context values."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_native_timer_creation import function

ROOT = Path(__file__).resolve().parents[1]


class SoftResetSessionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='moonshine-soft-reset-')
        cls.addClassCleanup(cls.temp.cleanup)
        entry = function((ROOT/'src/main.cpp').read_text(), 'onUpdate')
        entry = entry[:entry.index('static bool recordsStageContext')] + 'return 0;}\n'
        source = r'''
using s32=int;
namespace JDrama {struct TDirector{};}
struct TApplication {enum {CONTEXT_GAME_INTRO=4};unsigned char mContext;}gpApplication;
namespace CrashReport {void observeContext(unsigned char){}}
int cancelled=0;bool running;
namespace StageLoader {
bool active(){return running;}
void cancel(){++cancelled;running=false;}
}
''' + entry + r'''
extern "C" __declspec(dllexport) int tick(int context,int active) {
 gpApplication.mContext=5;running=false;onUpdate(nullptr);
 gpApplication.mContext=context;running=active;cancelled=0;
 onUpdate(nullptr);onUpdate(nullptr);
 return cancelled;
}
extern "C" __declspec(dllexport) int startAtFileSelect() {
 tick(4,1);running=true;cancelled=0;onUpdate(nullptr);
 return cancelled;
}
'''
        path = Path(cls.temp.name)/'reset.cpp'
        path.write_text(source)
        proc = subprocess.run([str(ROOT/'toolchain/clang++.exe'),
            '--target=x86_64-pc-windows-msvc','-shared','-nostdlib','-fuse-ld=lld',
            '-Wl,/noentry','-O2',str(path),'-o',str(path.with_suffix('.dll'))],
            capture_output=True,text=True)
        if proc.returncode: raise RuntimeError(proc.stderr)
        cls.lib = C.CDLL(str(path.with_suffix('.dll')))
        cls.addClassCleanup(lambda: C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def test_return_to_title_cancels_once_before_intro_or_file_select(self):
        self.assertEqual(self.lib.tick(4,1),1)
        self.assertEqual(self.lib.tick(4,0),0)

    def test_gameplay_death_movie_and_episode_select_retain_the_session(self):
        for context in (0,1,2,3,5,6,7,8,9):
            self.assertEqual(self.lib.tick(context,1),0,context)

    def test_new_session_from_intro_skip_file_select_is_allowed(self):
        self.assertEqual(self.lib.startAtFileSelect(),0)


if __name__ == '__main__': unittest.main()
