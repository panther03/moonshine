"""Exercise production ghost clocks and captured input/split timestamps."""
from pathlib import Path
import ctypes
import random
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class GhostTasClockTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory()
        folder = Path(cls.folder.name)
        source = (ROOT / "src/ghost.cpp").read_text()
        shim = r'''
#include "susamune/ghost_clock.h"
#include "susamune/ghost_format.h"
#include "susamune/susamune_cfg.h"
typedef unsigned int u32; typedef unsigned short u16;
typedef unsigned char u8; typedef int s32; typedef long long s64;
namespace GhostFludd {void capture(SusamuneGhostFluddSample&) {}}
struct Timer {
 s32 qf; u32 serial; bool stopped;
 bool currentQf(s32 *out, bool *stop=0) { *out=qf; if(stop)*stop=stopped; return true; }
 u32 attemptSerial() { return serial; }
} gQFTTimer;
struct Track {
 SusamuneGhostInputSample *inputs; u32 inputCount;
 SusamuneGhostSplitSample splits[6]; u8 splitCount;
 u16 formatVersion; bool completed; u16 segmentCount; u8 teachingFlags; u32 runFlags; u32 endQf;
} sRecord;
SusamuneGhostInputSample inputs[4096];
SusamuneGhostClock sRecordClock;
bool sFrameFrozen, sFrameAssisted, sRecording;
u32 sAttemptSerial;
enum { OBSERVER_OFF, OBSERVER_PREPARING_ONE, OBSERVER_PREPARING_TWO,
 OBSERVER_WARPING_ONE, OBSERVER_WARPING_TWO, OBSERVER_ACTIVE_ONE, OBSERVER_ACTIVE_TWO };
int sObserverPhase;
s32 sObserverBaseQf, sObserverLastQf, sObserverLastLiveQf, sObserverQfOffset, sObserverEndQf;
bool sObserverClockReady;
bool observerRunning() { return sObserverPhase==5 || sObserverPhase==6; }
bool observerStatsSuppressed() { return observerRunning(); }
'''
        body = "\n".join(function(source, signature) for signature in (
            "SusamuneGhostInputSample &inputAt(", "s32 recordQf(", "void frameControl(", "void captureInput(",
            "void captureSplit(", "s32 observerQf("))
        wrapper = r'''
extern "C" {
__declspec(dllexport) void reset() {
 gQFTTimer.qf=100;gQFTTimer.serial=1;gQFTTimer.stopped=false;
 sAttemptSerial=1;sRecordClock.ready=false;sRecordClock.omittedQf=0;
 sRecord.inputs=inputs;sRecord.inputCount=0;sRecord.splitCount=0;
 sRecord.completed=false;sRecord.segmentCount=1;sRecord.teachingFlags=0;
 sRecord.formatVersion=5;sRecord.runFlags=0;sRecording=true;sObserverPhase=0;
 sFrameFrozen=false;sFrameAssisted=false;
 frameControl(false,false);
}
__declspec(dllexport) int frame(int raw,int held,int assisted) {
 gQFTTimer.qf=raw;frameControl(held!=0,assisted!=0);return recordQf(raw);
}
__declspec(dllexport) void input(unsigned int button) {
 SusamunePracticeInput p;p.buttons=button;p.stickX=35;p.stickY=-60;
 p.substickX=42;p.substickY=-12;p.triggerL=190;p.triggerR=77;
 p.analogA=29;p.analogB=41;p.error=-1;p.flags=0;captureInput(p);
}
__declspec(dllexport) unsigned int count() {return sRecord.inputCount;}
__declspec(dllexport) unsigned int inputQf(unsigned int i) {return inputs[i].qf;}
__declspec(dllexport) unsigned int packetField(unsigned int i) {
 const unsigned char *p=(const unsigned char*)&inputs[0].input;return p[i];
}
__declspec(dllexport) unsigned int split(unsigned int raw,unsigned int endpoint) {
 captureSplit(3,endpoint,raw);return sRecord.splits[endpoint].qf;
}
__declspec(dllexport) int resetSerial(unsigned int serial,int raw) {
 gQFTTimer.serial=serial;sAttemptSerial=serial;gQFTTimer.qf=raw;
 return recordQf(raw);
}
__declspec(dllexport) int tasCarry(int raw,int held) {
 sRecord.runFlags=SUSAMUNE_GHOST_RUN_TAS|SUSAMUNE_GHOST_RUN_ASSISTED;
 gQFTTimer.qf=raw;frameControl(held!=0,false);return recordQf(raw);
}
__declspec(dllexport) void observer(int two) {
 sObserverPhase=two?6:5;sObserverBaseQf=0;sObserverEndQf=100000;
 sObserverLastQf=100;sObserverLastLiveQf=100;sObserverQfOffset=0;
 sObserverClockReady=true;gQFTTimer.qf=100;gQFTTimer.stopped=false;
}
__declspec(dllexport) int observe(int raw,int held) {
 gQFTTimer.qf=raw;frameControl(held!=0,false);return observerQf();
}
}
'''
        cpp, dll = folder / "clock.cpp", folder / "clock.dll"
        cpp.write_text(shim + body + wrapper)
        command = [str(ROOT / "toolchain/clang++.exe"),
                   "--target=x86_64-pc-windows-msvc", "-shared", "-nostdlib",
                   "-fuse-ld=lld", "-Wl,/noentry", "-fno-exceptions", "-fno-rtti",
                   "-I", str(ROOT / "include"), str(cpp), "-o", str(dll)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)
        cls.lib = ctypes.CDLL(str(dll))

    @classmethod
    def tearDownClass(cls):
        handle = cls.lib._handle
        del cls.lib
        ctypes.windll.kernel32.FreeLibrary(ctypes.c_void_p(handle))
        cls.folder.cleanup()

    def setUp(self):
        self.lib.reset()

    def test_normal_menu_time_is_preserved(self):
        self.assertEqual(self.lib.frame(104, 0, 0), 104)
        self.assertEqual(self.lib.frame(604, 1, 0), 604)
        self.assertEqual(self.lib.frame(608, 0, 0), 608)

    def test_pause_and_steps_share_input_and_split_clock(self):
        self.assertEqual(self.lib.frame(104, 0, 1), 104)
        self.lib.input(0x100)
        self.assertEqual(self.lib.frame(1104, 1, 1), 104)
        self.assertEqual(self.lib.frame(1104, 1, 1), 104)
        self.assertEqual(self.lib.frame(1108, 0, 1), 108)
        self.lib.input(0x200)
        self.assertEqual(self.lib.split(1108, 0), 108)
        self.assertEqual(self.lib.frame(2112, 1, 1), 108)
        self.assertEqual(self.lib.frame(2116, 0, 1), 112)
        self.lib.input(0x400)
        self.assertEqual(self.lib.split(2116, 1), 112)
        self.assertEqual(self.lib.count(), 3)
        self.assertEqual([self.lib.inputQf(i) for i in range(3)], [104, 108, 112])
        self.assertEqual([self.lib.packetField(i) for i in range(2, 12)],
                         [35, 196, 42, 244, 190, 77, 29, 41, 255, 0])

    def test_scene_gap_kept_and_tas_status_continues(self):
        self.assertEqual(self.lib.frame(220, 1, 1), 100)
        self.assertEqual(self.lib.frame(224, 0, 1), 104)
        self.assertEqual(self.lib.tasCarry(444, 0), 324)
        self.assertEqual(self.lib.tasCarry(544, 1), 324)
        self.assertEqual(self.lib.tasCarry(548, 0), 328)

    def test_restart_and_qft_regression_reset_offset(self):
        self.lib.frame(1100, 1, 1)
        self.assertEqual(self.lib.resetSerial(2, 20), 20)
        self.lib.frame(120, 1, 1)
        self.assertEqual(self.lib.frame(4, 0, 1), 4)

    def test_repeated_and_simultaneous_captures_do_not_shift_time(self):
        self.lib.frame(1104, 1, 1)
        self.lib.frame(1108, 0, 1)
        self.lib.input(0x100)
        self.lib.input(0x200)
        self.assertEqual(self.lib.count(), 1)
        self.assertEqual(self.lib.split(1108, 0), 104)
        self.assertEqual(self.lib.split(1108, 1), 104)

    def test_randomized_held_time_against_sum_of_permitted_deltas(self):
        rng = random.Random(981)
        raw = expected = 100
        for frame in range(2000):
            delta = rng.choice([0, 1, 2, 4, 5, 8])
            raw += delta
            held = rng.randrange(2)
            assisted = frame >= 20
            if not held or not assisted:
                expected += delta
            self.assertEqual(self.lib.frame(raw, held, assisted), expected)

    def test_watch_and_watch_two_hold_step_and_resume(self):
        for two in (0, 1):
            self.lib.observer(two)
            for raw, held, expected in [(104, 1, 100), (1104, 1, 100),
                                        (1108, 0, 104), (1112, 1, 104),
                                        (2112, 1, 104), (2116, 0, 108),
                                        (2120, 0, 112)]:
                self.assertEqual(self.lib.observe(raw, held), expected)
                self.assertEqual(self.lib.observe(raw, held), expected)
            self.lib.input(0x100)
            self.assertEqual(self.lib.count(), 0)


if __name__ == "__main__":
    unittest.main()
