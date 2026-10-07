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
        code += 'void*memcpy(void*d,const void*s,unsigned long long n){for(unsigned long long i=0;i<n;++i)((u8*)d)[i]=((const u8*)s)[i];return d;}\n'
        code += function(source, "configureMaterial")
        code += "enum Appearance{APPEARANCE_SHADOW,APPEARANCE_PIANTA,APPEARANCE_MARIO};\n"
        code += "struct ModelSlot{int index;}; ModelSlot sSlots[2]={{0},{1}};\n"
        code += "const int SETTING_GHOST_APPEARANCE=0;struct Settings{int value;int get(int){return value;}}gSettings;\n"
        code += function(model, "selectedAlternative") + function(model, "runnerSlot")
        code += '''
extern "C" __declspec(dllexport) void convert(u8*tev,u8*color,u8*original){configureMaterial(tev,color,original);}
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
        cls.lib.convert.argtypes = [C.POINTER(C.c_ubyte)] * 3

    def test_private_shader_preserves_texture_and_multiplies_opacity(self):
        tev = (C.c_ubyte * 168)(*([0xA5] * 168))
        color = (C.c_ubyte * 32)(*([0xA5] * 32))
        original = (C.c_ubyte * 0x12A)(*[(i * 13) & 255 for i in range(0x12A)])
        C.c_ushort.from_buffer(original, 4).value = 55
        C.c_ushort.from_buffer(original, 10).value = 58
        self.lib.convert(tev, color, original)
        self.assertEqual(C.c_ushort.from_buffer(tev, 4).value, 55)
        self.assertEqual(C.c_ushort.from_buffer(tev, 6).value, 58)
        self.assertEqual(bytes(tev[12:24]), bytes([0,0,4,0,3,1,4,0,255,255,5,0]))
        self.assertEqual(tev[0x1C], 3)
        # Clean base texture passes unchanged; native lighting stages retain
        # their RGB math and constants instead of texture * diffuse shading.
        self.assertEqual(bytes(tev[0x1D:0x21]), bytes.fromhex('c008fff8'))
        for stage, native in ((1,3),(2,4)):
            out=0x1D+stage*8; old=0x55+native*8
            self.assertEqual(bytes(tev[out+1:out+4]),bytes(original[old+1:old+4]))
            self.assertEqual((tev[0x6E+stage],tev[0x72+stage]),
                             (original[0x106+native],original[0x116+native]))
        self.assertEqual(bytes(tev[0x3E:0x6E]),bytes(original[0xD6:0x106]))
        self.assertEqual(bytes(tev[0x76:0x7A]),bytes(original[0x126:0x12A]))
        for stage in range(3):
            p=0x1D+stage*8
            # Alpha uses register opacity first, then carries PREV untouched.
            self.assertEqual((tev[p+6]>>5,(tev[p+6]>>2)&7,
                              ((tev[p+6]<<1)&6)|(tev[p+7]>>7),(tev[p+7]>>4)&7),
                             (7,7,7,5 if stage==0 else 0))
            self.assertEqual((tev[p],tev[p+4],tev[p+5]),(0xC0+stage*2,0xC1+stage*2,8))
        self.assertEqual(bytes(color[4:12]), b"\xa5" * 8)
        self.assertEqual(color[12], 2)
        for n in range(4):
            self.assertEqual(C.c_ushort.from_buffer(color, 14 + 2 * n).value,
                             0x400 if n == 1 else 0xA5A5)
        # Constructor/vtable bytes, cull choice, neighbouring storage untouched.
        for buffer, start in ((tev, 160), (color, 24)):
            self.assertEqual(bytes(buffer[:4]), b"\xa5" * 4)
            self.assertEqual(bytes(buffer[start:]), b"\xa5" * 8)
        self.assertEqual(color[22], 0xA5)

    def test_retail_diffuse_lighting_survives_private_conversion(self):
        tev = (C.c_ubyte * 168)()
        original = (C.c_ubyte * 0x12A)()
        color = (C.c_ubyte * 32)()
        # All eleven retail Mario materials use enabled channel 0, two lights,
        # signed diffuse and spot attenuation, with register ambient/material.
        native_diffuse = 2 | (3 << 2) | (1 << 7) | (1 << 9) | (1 << 10)
        C.c_ushort.from_buffer(color, 14).value = native_diffuse
        self.lib.convert(tev, color, original)
        self.assertEqual(C.c_ushort.from_buffer(color, 14).value, native_diffuse)
        self.assertEqual(C.c_ushort.from_buffer(color, 16).value, 0x400)

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
