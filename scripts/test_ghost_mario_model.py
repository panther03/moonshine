"""Execute Mario's private material conversion and retain old appearance values."""
import ast
import ctypes as C
import _ctypes
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

import test_iling_episode_persistence as persistence
from test_native_timer_creation import function

ROOT = Path(__file__).resolve().parents[1]


class GhostMarioMaterialTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / "toolchain/clang++.exe"
        if not compiler.exists():
            raise unittest.SkipTest("Bundled Windows compiler required")
        cls.temp = tempfile.TemporaryDirectory(prefix="moonshine-mario-material-")
        cls.addClassCleanup(cls.temp.cleanup)
        directory = Path(cls.temp.name)
        source = (ROOT / "src/ghost_mario_model.cpp").read_text()
        model = (ROOT / "src/ghost_model.cpp").read_text()
        code = "typedef unsigned char u8;typedef unsigned short u16;typedef unsigned u32;\n"
        code += function(source, "configureMaterial")
        code += "enum Appearance{APPEARANCE_SHADOW,APPEARANCE_PIANTA,APPEARANCE_MARIO};\n"
        code += "struct ModelSlot{int index;}; ModelSlot sSlots[2]={{0},{1}};\n"
        code += "const int SETTING_GHOST_APPEARANCE=0;struct Settings{int value;int get(int){return value;}}gSettings;\n"
        code += function(model, "selectedAlternative") + function(model, "runnerSlot")
        code += '''
extern "C" __declspec(dllexport) void convert(u8*tev,u8*texgen,u8*color,u16 texture){configureMaterial(tev,texgen,color,texture);}
extern "C" __declspec(dllexport) int role(int appearance,int runner){gSettings.value=appearance;return runnerSlot(runner).index;}
extern "C" __declspec(dllexport) int alternative(int appearance){gSettings.value=appearance;return selectedAlternative();}
'''
        cpp = directory / "test.cpp"
        dll = directory / "test.dll"
        cpp.write_text(code)
        subprocess.run([str(compiler), "--target=x86_64-pc-windows-msvc", "-shared", "-nostdlib",
                        "-fno-builtin", "-fuse-ld=lld", "-Wl,/noentry", "-O2", str(cpp), "-o", str(dll)],
                       check=True, capture_output=True)
        cls.lib = C.CDLL(str(dll))
        cls.addClassCleanup(lambda: _ctypes.FreeLibrary(cls.lib._handle))
        cls.lib.convert.argtypes = [C.POINTER(C.c_ubyte)] * 3 + [C.c_ushort]

    def test_private_shader_preserves_texture_and_multiplies_opacity(self):
        tev = (C.c_ubyte * 40)(*([0xA5] * 40))
        texgen = (C.c_ubyte * 100)(*([0xA5] * 100))
        color = (C.c_ubyte * 32)(*([0xA5] * 32))
        self.lib.convert(tev, texgen, color, 55)
        self.assertEqual(C.c_ushort.from_buffer(tev, 4).value, 55)
        self.assertEqual(bytes(tev[6:9]), b"\0\0\4")
        # Decode retail GX TEV register fields: (zero + texture * raster)
        self.assertEqual((tev[12] >> 4, tev[12] & 15, tev[13] >> 4, tev[13] & 15), (15, 8, 10, 15))
        self.assertEqual((tev[16] >> 5, (tev[16] >> 2) & 7,
                          ((tev[16] << 1) & 6) | (tev[17] >> 7), (tev[17] >> 4) & 7), (7, 4, 5, 7))
        self.assertEqual(tev[11], 8)  # ADD, zero bias, x1, clamp, PREV
        self.assertEqual(tev[15], 8)
        self.assertEqual(bytes(color[4:12]), b"\xff" * 8)
        self.assertEqual(color[12], 1)
        for n in range(4):
            self.assertEqual(C.c_ushort.from_buffer(color, 14 + 2 * n).value, 0x400)
        for n in range(8):
            self.assertEqual(bytes(texgen[8+4*n:11+4*n]), bytes([1, 4, 60]))
        self.assertEqual(bytes(texgen[0x28:0x48]), bytes(32))
        # Constructor/vtable bytes, cull choice, neighbouring storage untouched.
        for buffer, start in ((tev, 32), (texgen, 92), (color, 24)):
            self.assertEqual(bytes(buffer[:4]), b"\xa5" * 4)
            self.assertEqual(bytes(buffer[start:]), b"\xa5" * 8)
        self.assertEqual(color[22], 0xA5)

    def test_three_appearances_use_only_two_physical_slots(self):
        for appearance, expected in ((0, (0, 1)), (1, (1, 0)), (2, (1, 0))):
            self.assertEqual(tuple(self.lib.role(appearance, r) for r in range(2)), expected)
            self.assertEqual(self.lib.alternative(appearance), 2 if appearance == 2 else 1)

    def test_choice_mapping_preserves_existing_options(self):
        source = (ROOT / "src/settings.cpp").read_text()
        labels_text = source.split("const char kChoiceLabels[] =", 1)[1].split(";", 1)[0]
        labels = "".join(ast.literal_eval(s) for s in re.findall(r'"(?:\\.|[^"\\])*"', labels_text)).split("\0")
        def numbers(name):
            raw = source.split(name, 1)[1].split("{", 1)[1].split("}", 1)[0]
            raw = re.sub(r"//[^\n]*", "", raw)
            return [int(n) for n in re.findall(r"\d+", raw)]
        first = numbers("const u8 kChoiceFirst")
        choices = numbers("const u8 kChoiceMap")
        self.assertEqual([labels[i] for i in choices[first[10]:first[11]]],
                         ["Shadow Mario", "Piantissimo", "Mario"])
        self.assertEqual([b-a for a,b in zip(first, first[1:])],
                         [2,3,4,3,3,6,3,3,4,3,3,3,3,2,6,2,5,4,4,2,3,4,33,25,11,5,16,4])
        self.assertEqual(first[-1], len(choices))


class GhostAppearancePersistenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        persistence.EpisodePersistenceTests.setUpClass.__func__(cls)

    def test_old_and_new_appearance_values_survive_each_region(self):
        keys = re.findall(r'X\(\s*SETTING_\w+,\s*"([^"]+)"\)',
                          (ROOT / "include/susamune/settings_list.h").read_text())
        index = keys.index("ghost_appearance")
        for region in (b"jp", b"us", b"pal"):
            self.lib.selectRegion(region)
            for appearance in (0, 1, 2):
                text = b"[settings_" + region + b"]\r\nghost_appearance = " + str(appearance).encode() + b"\r\n"
                self.lib.parse(text)
                self.assertEqual(self.lib.getSetting(index), appearance)
                rewritten = self.lib.rewrite(text)
                self.assertIn(b"ghost_appearance = " + str(appearance).encode(), rewritten)
                self.lib.parse(rewritten)
                self.assertEqual(self.lib.getSetting(index), appearance)


if __name__ == "__main__":
    unittest.main()
