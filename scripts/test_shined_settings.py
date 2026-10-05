"""Exercise packed favourites, including old saves and newly named settings."""
import ctypes as C
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from test_practice_tape import function_source

ROOT = Path(__file__).resolve().parents[1]


class ShinedSettingsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / "toolchain/clang++.exe"
        if not compiler.exists():
            raise unittest.SkipTest("Bundled Windows compiler required")
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.ids = re.findall(r"X\((SETTING_\w+),", (ROOT / "include/susamune/settings_list.h").read_text())
        cls.names = re.findall(r'^S(?:BOOL|CHOICE)\("([^"]*)"', (ROOT / "src/settings_descs.inc").read_text(), re.M)
        production = ROOT / "src/settings.cpp"
        methods = "\n".join(function_source(production, signature) for signature in (
            "int rngFavoriteBit(", "static int extraFavoriteIndex(", "static SettingId extraFavoriteBank(", "static unsigned favoriteLocation(", "void Settings::set(", "bool Settings::favoriteable(",
            "bool Settings::favorite(", "void Settings::toggleFavorite(", "const char *Settings::name("))
        source = Path(cls.tmp.name) / "shined.cpp"
        source.write_text(r'''
#include "susamune/settings.hxx"
#include "susamune/packed_text.hxx"
extern "C" { int _fltused; }
static Settings settings;
static const int kSettingDescs[SETTING_COUNT]={};
static u8 choiceCount(int){return 2;}
#define SBOOL(name,def,cat) name "\0"
#define SCHOICE(name,def,choices,cat) name "\0"
static const char kSettingNames[]=
#include "settings_descs.inc"
;
#undef SBOOL
#undef SCHOICE
''' + methods + r'''
extern "C" __declspec(dllexport) void reset(){for(int i=0;i<SETTING_COUNT;++i)settings.set((SettingId)i,0);}
extern "C" __declspec(dllexport) void set(unsigned id,unsigned value){settings.set((SettingId)id,(u8)value);}
extern "C" __declspec(dllexport) unsigned get(unsigned id){return settings.get((SettingId)id);}
extern "C" __declspec(dllexport) unsigned eligible(int id){return Settings::favoriteable((SettingId)id);}
extern "C" __declspec(dllexport) unsigned favorite(int id){return settings.favorite((SettingId)id);}
extern "C" __declspec(dllexport) void toggle(int id){settings.toggleFavorite((SettingId)id);}
''', encoding="ascii")
        dll = source.with_suffix(".dll")
        subprocess.run([str(compiler), "--target=x86_64-pc-windows-msvc", "-shared",
                        "-nostdlib", "-fuse-ld=lld", "-Wl,/noentry", "-O2",
                        "-I", str(ROOT / "include"), "-I", str(ROOT / "src"),
                        str(source), str(ROOT / "src/packed_text.cpp"), "-o", str(dll)], check=True)
        cls.lib = C.CDLL(str(dll))
        cls.addClassCleanup(lambda: C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def setUp(self):
        self.lib.reset()

    def test_every_named_setting_can_be_shined_without_changing_its_value(self):
        for i, name in enumerate(self.names):
            self.assertEqual(bool(self.lib.eligible(i)), bool(name), self.ids[i])
            if not name:
                continue
            self.lib.set(i, 1)
            self.lib.toggle(i)
            self.assertTrue(self.lib.favorite(i), self.ids[i])
            self.assertEqual(self.lib.get(i), 1, self.ids[i])
        for i, name in enumerate(self.names):
            self.assertEqual(bool(self.lib.favorite(i)), bool(name), self.ids[i])

    def test_packed_bytes_restore_all_favourites_independently(self):
        wanted = {i for i, name in enumerate(self.names) if name and i % 3 != 1}
        for i in wanted:
            self.lib.toggle(i)
        banks = [i for i, name in enumerate(self.ids) if "FAVORITES" in name]
        saved = {i: self.lib.get(i) for i in banks}
        self.lib.reset()
        for i, value in saved.items():
            self.lib.set(i, value)
        self.assertEqual({i for i in range(len(self.ids)) if self.lib.favorite(i)}, wanted)
        for i in wanted:
            self.lib.toggle(i)
        self.assertTrue(all(self.lib.get(i) == 0 for i in banks))

    def test_existing_favourite_bank_indices_keep_their_meaning(self):
        first = self.ids.index("SETTING_FAVORITES_0")
        self.assertEqual(first, 77)
        self.assertEqual(self.ids.index("SETTING_RNG_FAVORITES"), 117)
        for i in range(first):
            if not self.names[i]:
                continue
            self.lib.reset()
            self.lib.set(first + i // 7, 1 << (i % 7))
            self.assertEqual([j for j in range(len(self.ids)) if self.lib.favorite(j)], [i])
        rng = ["SETTING_KING_BOO_ALWAYS_FRUIT", "SETTING_PETEY_NO_TORNADO", "SETTING_PETEY_ROUTE",
               "SETTING_RICCO_CRANE_SPEED", "SETTING_RICCO_FRUIT_MACHINE",
               "SETTING_GELATO_RED_COIN_FISH_PATTERN", "SETTING_GELATO_BLUE_BIRD_PATTERN"]
        for bit, name in enumerate(rng):
            self.lib.reset()
            self.lib.set(117, 1 << bit)
            self.assertEqual([j for j in range(len(self.ids)) if self.lib.favorite(j)], [self.ids.index(name)])

    def test_internal_and_out_of_range_ids_cannot_modify_storage(self):
        before = [self.lib.get(i) for i in range(len(self.ids))]
        for i in [-1, len(self.ids) + 1, 999] + [i for i, name in enumerate(self.names) if not name]:
            self.assertFalse(self.lib.eligible(i))
            self.assertFalse(self.lib.favorite(i))
            self.lib.toggle(i)
        self.assertEqual([self.lib.get(i) for i in range(len(self.ids))], before)

    def test_jump_and_buttslide_have_independent_persisted_favourite_bits(self):
        jump, buttslide = self.ids.index('SETTING_JUMP_DISPLAY'), len(self.ids)
        self.assertTrue(self.lib.eligible(buttslide))
        banks = [i for i, name in enumerate(self.ids) if 'FAVORITES' in name]
        for chosen in (jump, buttslide):
            self.lib.reset()
            self.lib.toggle(chosen)
            self.assertEqual([i for i in (jump, buttslide) if self.lib.favorite(i)], [chosen])
            saved = {i:self.lib.get(i) for i in banks}
            self.lib.reset()
            for i,value in saved.items():self.lib.set(i,value)
            self.assertEqual([i for i in (jump, buttslide) if self.lib.favorite(i)], [chosen])
            self.lib.toggle(jump if chosen == buttslide else buttslide)
            self.assertTrue(self.lib.favorite(jump))
            self.assertTrue(self.lib.favorite(buttslide))
            self.assertEqual(self.lib.get(jump), 0)


if __name__ == "__main__":
    unittest.main()
