"""Exercise production ready-frame gates and Watch's movie-only skip consumer."""

import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_practice_tape import function_source

ROOT = Path(__file__).resolve().parents[1]


class CinematicInputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / "toolchain/clang++.exe"
        if not compiler.exists():
            raise unittest.SkipTest("Bundled Windows compiler required")
        cls.folder = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.folder.cleanup)
        context = function_source(ROOT / "src/retail_input.cpp", "Context context()")
        watch = function_source(ROOT / "src/ghost.cpp", "void beforeDirect()")
        source = Path(cls.folder.name) / "cinema.cpp"
        source.write_text(r'''
using u32=unsigned long long; using s32=int;
struct TMarioGamePad {unsigned mFrameMeaning; unsigned rawButtons;};
struct TMarDirector {enum {STATE_NORMAL=4}; unsigned _260,mCurState;};
struct TMovieDirector {int mState; TMarioGamePad *mGamePad; unsigned mFlags;};
struct Application {TMarioGamePad *mGamePads[1]; unsigned mCutSceneID;} gpApplication;
static TMarDirector stage; static TMovieDirector movie; static TMarioGamePad pad;
static int actualType; static bool workerDone; static int gSetupThread;
bool OSIsThreadTerminated(int*) {return workerDone;}
namespace RetailInput {
enum Context {Unavailable,StageLoading,StageReady,MovieLoading,MovieReady};
TMarDirector *stageDirector() {return actualType==1 ? &stage : nullptr;}
TMovieDirector *movieDirector() {return actualType==2 ? &movie : nullptr;}
''' + context + r'''
}
namespace GhostFludd {void beginFrame(){}}
static bool watching, sFrameFrozen, sObserverMarioBaselineFinalized, sObserverStageReady;
static bool sObserverPastEnd;
static int sObserverPhase, anchored;
enum {OBSERVER_ACTIVE_ONE=1,OBSERVER_ACTIVE_TWO=2};
static TMarDirector *gpMarDirector=&stage;
bool observerRunning() {return watching;}
void releaseObserverMario(bool) {}
s32 observerQf(bool*) {return 0;}
void updateObserverVisual(s32) {}
void anchorObserverMario() {++anchored;}
''' + watch + r'''
extern "C" __declspec(dllexport) unsigned ready(unsigned type,unsigned flags,unsigned movieId,u32 padAddress) {
    actualType=type; workerDone=flags&1;
    stage._260=flags&2; movie.mFlags=(flags&2)?1:0;
    gpApplication.mGamePads[0]=reinterpret_cast<TMarioGamePad*>(padAddress);
    movie.mGamePad=(flags&4)?nullptr:gpApplication.mGamePads[0];
    gpApplication.mCutSceneID=movieId;
    return RetailInput::context();
}
extern "C" __declspec(dllexport) unsigned skip(unsigned type,unsigned flags,int state) {
    actualType=type; watching=flags&1; movie.mFlags=(flags&2)?1:0;
    sObserverStageReady=true; sObserverMarioBaselineFinalized=false; sObserverPhase=0;
    stage._260=1; stage.mCurState=4; movie.mState=state;
    gpApplication.mGamePads[0]=&pad; movie.mGamePad=(flags&4)?nullptr:&pad;
    pad.mFrameMeaning=0x400;pad.rawButtons=0;anchored=0;
    beforeDirect();
    return pad.mFrameMeaning | (pad.rawButtons<<16) | (anchored<<24);
}
''', encoding="ascii")
        library = source.with_suffix(".dll")
        subprocess.run([str(compiler), "--target=x86_64-pc-windows-msvc", "-shared",
                        "-nostdlib", "-fuse-ld=lld", "-Wl,/noentry", "-O2",
                        str(source), "-o", str(library)], check=True)
        cls.lib = C.CDLL(str(library))
        cls.lib.ready.argtypes = [C.c_uint, C.c_uint, C.c_uint, C.c_uint64]
        cls.addClassCleanup(lambda: C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def test_first_ready_direct_call_can_consume_input_before_setup_flag(self):
        for kind, loading, ready in ((1, 1, 2), (2, 3, 4)):
            self.assertEqual(self.lib.ready(kind, 0, 2, 0x80400000), loading)
            self.assertEqual(self.lib.ready(kind, 1, 2, 0x80400000), ready)
            self.assertEqual(self.lib.ready(kind, 2, 2, 0x80400000), ready)

    def test_unknown_directors_and_invalid_pads_do_not_consume(self):
        self.assertEqual(self.lib.ready(0, 3, 2, 0x80400000), 0)
        for address in (0, 0x7ffffffc, 0x80400001, 0x817fff04, 0x180400000):
            self.assertEqual(self.lib.ready(2, 3, 2, address), 0)
        self.assertEqual(self.lib.ready(2, 7, 2, 0x80400000), 0)
        self.assertEqual(self.lib.ready(2, 3, 20, 0x80400000), 0)

    def test_watch_skips_only_a_ready_movie_playback(self):
        for state in (0, 1):
            self.assertEqual(self.lib.skip(2, 3, state), 0x420)
        for state in (-1, 2, 3, 4, 5):
            self.assertEqual(self.lib.skip(2, 3, state), 0x400)

    def test_live_player_racing_and_loading_receive_no_synthetic_input(self):
        for flags in (0, 1, 2, 7):
            self.assertEqual(self.lib.skip(2, flags, 1), 0x400)
        self.assertEqual(self.lib.skip(0, 3, 1), 0x400)
        self.assertEqual(self.lib.skip(1, 3, 1), 0x1000400)

    def test_all_regions_use_primary_retail_vtables(self):
        source = (ROOT / "src/retail_input.cpp").read_text().lower()
        for region, stage, movie in (("jp", "803b3ca0", "803b48d8"),
                                     ("us", "803df0c8", "803dfa50"),
                                     ("pal", "803d68a8", "803d73b8")):
            symbols = (ROOT / f"maps/{region}.map").read_text().lower()
            for address, name in ((stage, "__vt__12tmardirector"),
                                  (movie, "__vt__14tmoviedirector")):
                self.assertIn("0x" + address, source)
                self.assertTrue(any(address in row and name in row for row in symbols.splitlines()))


if __name__ == "__main__":
    unittest.main()
