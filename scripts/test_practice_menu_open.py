"""The menu-opening input must freeze the same frame that ends an input take."""

import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class MenuOpeningHoldTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / "toolchain/clang++.exe"
        if not compiler.exists():
            raise unittest.SkipTest("Bundled Windows compiler required")
        cls.folder = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.folder.cleanup)
        source = (ROOT / "src/main.cpp").read_text()
        expressions = []
        for signature in ("const bool menuOpenBeforeDirect =", "bool menuOwnsRetailPad =",
                          "const bool freeze ="):
            expressions.append(signature + source.split(signature, 1)[1].split(";", 1)[0] + ";")
        program = Path(cls.folder.name) / "menu_open.cpp"
        program.write_text(r'''
struct TMarDirector {enum {STATE_NORMAL=4}; unsigned mCurState; unsigned _260;};
static bool menuShown,menuPressed,wheelShown,practiceHold,saveDialog;
struct SavestateManager{static bool saveDialogOpen(){return saveDialog;}};
struct Menu {bool shown(){return menuShown;}};
struct Binds {bool wasPressedRaw(int){return menuPressed;}} gBinds;
const int BIND_MENU_TOGGLE=0;
namespace WarpWheel {bool shown(){return wheelShown;}}
namespace PracticeSession {bool freezeRequested(){return practiceHold;}}
extern "C" __declspec(dllexport) unsigned opening(unsigned flags,unsigned state) {
    Menu menu;Menu *gMenu=(flags&128)?nullptr:&menu;
    TMarDirector director={state,1};TMarDirector *stageDirector=(flags&1)?&director:nullptr;
    menuShown=flags&2;menuPressed=flags&4;wheelShown=flags&8;practiceHold=flags&32;saveDialog=flags&512;
    const bool sessionModalBeforeDirect=flags&16,stateDiskBusy=flags&64;
    const bool stepOverridesShortcut=flags&256,tasCinematic=false;
''' + "\n".join(expressions) + r'''
    return freeze;
}
''', encoding="ascii")
        library = program.with_suffix(".dll")
        subprocess.run([str(compiler), "--target=x86_64-pc-windows-msvc", "-shared",
                        "-nostdlib", "-fuse-ld=lld", "-Wl,/noentry", "-O2",
                        str(program), "-o", str(library)], check=True)
        cls.lib = C.CDLL(str(library))
        cls.addClassCleanup(lambda: C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def test_first_opening_frame_holds_before_menu_is_shown(self):
        self.assertEqual(self.lib.opening(1 | 4, 4), 1)
        self.assertEqual(self.lib.opening(1 | 2, 4), 1)
        self.assertEqual(self.lib.opening(1 | 2 | 4, 4), 1)

    def test_boot_cutscene_and_ordinary_gameplay_are_not_held(self):
        self.assertEqual(self.lib.opening(4, 4), 0)  # no stage director
        self.assertEqual(self.lib.opening(1, 4), 0)
        self.assertEqual(self.lib.opening(1 | 4 | 128, 4), 0)  # no menu yet
        for state in (0, 1, 2, 5, 9, 12):
            self.assertEqual(self.lib.opening(1 | 4, state), 0)

    def test_existing_modal_practice_and_storage_holds_still_apply(self):
        for owner in (8, 16, 32, 64):
            self.assertEqual(self.lib.opening(1 | owner, 4), 1)

    def test_paused_advance_overrides_a_new_menu_chord_but_never_an_open_menu(self):
        self.assertEqual(self.lib.opening(1 | 4 | 256, 4), 0)
        self.assertEqual(self.lib.opening(1 | 2 | 4 | 256, 4), 1)

    def test_sd_load_holds_save_box_until_transfer_finishes(self):
        for state in (5,11):
            self.assertEqual(self.lib.opening(1 | 64 | 512, state), 1)
            self.assertEqual(self.lib.opening(1 | 512, state), 0)
            self.assertEqual(self.lib.opening(1 | 64, state), 0)
        source=(ROOT/'src/main.cpp').read_text()
        self.assertIn('gpMarDirector->mCurState = heldDirectorState;',source)


if __name__ == "__main__":
    unittest.main()
