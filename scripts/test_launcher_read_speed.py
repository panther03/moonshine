"""Run the real drive-timing code before executable patching occurs."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LauncherReadSpeedTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT/'toolchain/clang.exe'
        if not compiler.exists(): raise unittest.SkipTest('Bundled Windows compiler required')
        cls.temp = tempfile.TemporaryDirectory(prefix='moonshine-read-speed-')
        cls.addClassCleanup(cls.temp.cleanup)
        source = (ROOT/'launcher/kernel/ReadSpeed.c').read_text()
        source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
        fixture = r'''
typedef unsigned int u32; typedef volatile unsigned int vu32;
int _fltused;
#define UINT_MAX 0xFFFFFFFFu
#define NIN_CFG_REMLIMIT 1
#define HW_TIMER 0
#define ALIGN_BACKWARD(x,n) ((x)&~((n)-1u))
#define dbgprintf(...) ((void)0)
static u32 config,ticks;
static u32 ConfigGetConfig(u32 mask){return config&mask;}
static u32 read32(u32 address){(void)address;return ticks;}
static u32 TimerDiffTicks(u32 begin){return ticks-begin;}
'''
        exports = r'''
#define API __declspec(dllexport)
API void reset(u32 unlocked){config=unlocked; ticks=0; ReadSpeed_Init();}
API void clockTicks(u32 value){ticks=value;}
API u32 limited(void){return UseReadLimit;}
API void begin(u32 bytes){ReadSpeed_Start();ReadSpeed_Setup(0x50000,bytes);}
'''
        path=Path(cls.temp.name)/'read_speed.c'
        path.write_text(fixture+source+exports)
        library=path.with_suffix('.dll')
        subprocess.run([str(compiler),'--target=x86_64-pc-windows-msvc','-shared','-O2',
                        '-nostdlib','-fuse-ld=lld','-Wl,/noentry','-Wl,/export:ReadSpeed_End',
                        '-Wl,/export:ReadSpeed_Motor',str(path),'-o',str(library)],check=True)
        cls.lib=C.CDLL(str(library))
        cls.addClassCleanup(lambda:C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def test_unlocked_boot_reads_do_not_wait_for_simulated_drive(self):
        self.lib.reset(1)
        self.assertEqual(self.lib.limited(),0)
        for length in (32,0x8000,0x80000):
            self.lib.begin(length)
            self.assertEqual(self.lib.ReadSpeed_End(),1)
        self.lib.ReadSpeed_Motor()
        self.assertEqual(self.lib.ReadSpeed_End(),1)

    def test_locked_setting_still_emulates_read_and_motor_delays(self):
        self.lib.reset(0)
        self.assertEqual(self.lib.limited(),1)
        self.lib.begin(0x80000)
        self.assertEqual(self.lib.ReadSpeed_End(),0)
        self.lib.clockTicks(1000000)
        self.assertEqual(self.lib.ReadSpeed_End(),1)
        self.lib.ReadSpeed_Motor()
        self.assertEqual(self.lib.ReadSpeed_End(),0)
        self.lib.clockTicks(2000000)
        self.assertEqual(self.lib.ReadSpeed_End(),1)

    def test_reinitialization_adopts_current_setting_both_directions(self):
        for enabled in (1,0,1,0):
            self.lib.reset(enabled)
            self.lib.begin(0x80000)
            self.assertEqual(self.lib.ReadSpeed_End(),enabled)


if __name__=='__main__':unittest.main()
