"""Hash only audited presentation exclusions; keep every other setting guarded."""

import ctypes as C
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_practice_tape import function_source

ROOT = Path(__file__).resolve().parents[1]


class ReplaySettingsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / "toolchain/clang++.exe"
        if not compiler.exists():
            raise unittest.SkipTest("Bundled Windows compiler required")
        cls.folder = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.folder.cleanup)
        cls.ids = re.findall(r"X\((SETTING_\w+),", (ROOT / "include/susamune/settings_list.h").read_text())
        production = ROOT / "src/practice_session.cpp"
        functions = "\n".join(function_source(production, signature) for signature in (
            "u32 hashBytes(", "bool replayPresentationSetting(", "u32 settingsHash()"))
        source = Path(cls.folder.name) / "settings.cpp"
        source.write_text(r'''
#include "susamune/settings.hxx"
extern "C" { int _fltused; }
static Settings settings;
Settings &gSettings=settings;
void Settings::set(SettingId id,u8 value){mValues[id]=value;}
static u32 stickMode;
static const size_t kStickMode=reinterpret_cast<size_t>(&stickMode);
static float cadence;
float SMSGetVSyncTimesPerSec(){return cadence;}
extern "C" void *memset(void *dst,int value,size_t n) {
    for(size_t i=0;i<n;++i)((volatile u8*)dst)[i]=(u8)value;return dst;
}
''' + functions + r'''
extern "C" __declspec(dllexport) void reset() {
    for(int i=0;i<SETTING_COUNT;++i)gSettings.set((SettingId)i,0);
    cadence=60;stickMode=1;
}
extern "C" __declspec(dllexport) void setting(u32 id,u32 value) {
    gSettings.set((SettingId)id,(u8)value);
}
extern "C" __declspec(dllexport) u32 hash(){return settingsHash();}
extern "C" __declspec(dllexport) u32 excluded(u32 id) {
    return replayPresentationSetting((SettingId)id);
}
extern "C" __declspec(dllexport) void mode(u32 value){stickMode=value;}
extern "C" __declspec(dllexport) void rate(float value){cadence=value;}
''', encoding="ascii")
        library = source.with_suffix(".dll")
        subprocess.run([str(compiler), "--target=x86_64-pc-windows-msvc", "-shared",
                        "-nostdlib", "-fuse-ld=lld", "-Wl,/noentry", "-O2",
                        "-I", str(ROOT / "include"), str(source), "-o", str(library)], check=True)
        cls.lib = C.CDLL(str(library))
        cls.addClassCleanup(lambda: C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))
        cls.lib.hash.restype = C.c_uint
        cls.lib.rate.argtypes = [C.c_float]

    def setUp(self):
        self.lib.reset()

    def test_allowlist_contains_only_audited_presentation_settings(self):
        expected = {f"SETTING_FAVORITES_{i}" for i in range(11)} | {
            f"SETTING_FAVORITES_EXTRA_{i}" for i in range(10)} | {
            "SETTING_RNG_FAVORITES", "SETTING_NATIVE_TIMER_X", "SETTING_NATIVE_TIMER_Y",
            "SETTING_NATIVE_TIMER_SCALE", "SETTING_FREE_CAMERA_SPEED",
            "SETTING_FREE_CAMERA_SENSITIVITY",
            "SETTING_FREE_CAMERA_SMOOTHING", "SETTING_FREE_CAMERA_HIDE_HUD",
            "SETTING_FREE_CAMERA_STRAFE_REVERSE", "SETTING_METADATA_HORIZONTAL",
            "SETTING_GHOST_INPUTS", "SETTING_TAS_BANNER",
            "SETTING_GB_SKIP_DISPLAY", "SETTING_JUMP_DISPLAY",
            "SETTING_SYSTEM_MESSAGES", "SETTING_SAVESTATE_ERRORS",
            "SETTING_STREAK_FAILURE_BANNER", "SETTING_STREAK_FAILURE_X",
            "SETTING_STREAK_FAILURE_Y", "SETTING_STREAK_FAILURE_SIZE",
            "SETTING_STREAK_FAILURE_DURATION"}
        actual = {name for i, name in enumerate(self.ids) if self.lib.excluded(i)}
        self.assertEqual(actual, expected)

    def test_presentation_changes_keep_take_compatible(self):
        baseline = self.lib.hash()
        for index, name in enumerate(self.ids):
            if not self.lib.excluded(index):
                continue
            for value in (1, 7, 127):
                self.lib.setting(index, value)
                self.assertEqual(self.lib.hash(), baseline, (name, value))

    def test_every_other_setting_change_still_rejects_the_take(self):
        baseline = self.lib.hash()
        for index, name in enumerate(self.ids):
            if self.lib.excluded(index):
                continue
            self.lib.setting(index, 1)
            self.assertNotEqual(self.lib.hash(), baseline, name)
            self.lib.setting(index, 0)

    def test_frame_rate_and_retail_stick_decoder_mode_remain_guarded(self):
        baseline = self.lib.hash()
        self.lib.rate(50)
        self.assertNotEqual(self.lib.hash(), baseline)
        self.lib.rate(60)
        self.assertEqual(self.lib.hash(), baseline)
        self.lib.mode(0)
        self.assertNotEqual(self.lib.hash(), baseline)


if __name__ == "__main__":
    unittest.main()
