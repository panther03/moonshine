"""Execute the attachment callback against a stateful GX test double."""
import ctypes as C
import _ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_native_timer_creation import function

ROOT = Path(__file__).resolve().parents[1]


class GhostFluddRenderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / "toolchain/clang++.exe"
        if not compiler.exists():
            raise unittest.SkipTest("Bundled Windows compiler required")
        cls.temp = tempfile.TemporaryDirectory(prefix="moonshine-fludd-render-")
        cls.addClassCleanup(cls.temp.cleanup)
        directory = Path(cls.temp.name)
        source = (ROOT / "src/ghost_model.cpp").read_text()
        code = r'''
using u8=unsigned char;
struct GXColor{u8 r,g,b,a;}; struct GXColorS10{short r,g,b,a;};
struct AttachmentPacketState{GXColor channelColor;GXColorS10 tevColor;bool tintTevColor;};
struct J3DShapePacket{};
enum{GX_COLOR0A0=1,GX_TEVREG2,GX_ALWAYS,GX_AOP_AND,GX_BM_BLEND,
     GX_BL_SRCALPHA,GX_BL_INVSRCALPHA,GX_LO_COPY,GX_TRUE,GX_LEQUAL,GX_FALSE};
const int kShapePacketUserAreaOffset=12;
int calls,blend,zwrite,alpha;
void GXSetChanMatColor(int,GXColor c){calls|=1;alpha=c.a;}
void GXSetTevColorS10(int,GXColorS10){calls|=2;}
void GXSetAlphaCompare(int,int,int,int,int){calls|=4;}
void GXSetBlendMode(int mode,int src,int dst,int op){calls|=8;blend=mode*100+src*10+dst;}
void GXSetZMode(int,int,int write){calls|=16;zwrite=write;}
'''
        code += function(source, "attachmentPacketCallback")
        code += r'''
extern "C" __declspec(dllexport) int run(int opacity,int tint,int phase,int valid){
  AttachmentPacketState state={{255,255,255,(u8)opacity},{0,0,0,0},tint!=0};
  alignas(16) u8 packet[32]={};
  *reinterpret_cast<AttachmentPacketState**>(packet+12)=valid?&state:nullptr;
  calls=0;blend=999;zwrite=999;alpha=-1;
  attachmentPacketCallback(reinterpret_cast<J3DShapePacket*>(packet),phase);
  return calls;
}
extern "C" __declspec(dllexport) int renderedAlpha(){return alpha;}
extern "C" __declspec(dllexport) int nativeBlendPreserved(){return blend==999&&zwrite==999;}
extern "C" __declspec(dllexport) int ghostBlendApplied(){return blend==GX_BM_BLEND*100+GX_BL_SRCALPHA*10+GX_BL_INVSRCALPHA&&zwrite==GX_FALSE;}
'''
        cpp, dll = directory / "test.cpp", directory / "test.dll"
        cpp.write_text(code)
        subprocess.run([str(compiler), "--target=x86_64-pc-windows-msvc", "-shared", "-nostdlib",
                        "-fno-builtin", "-fuse-ld=lld", "-Wl,/noentry", "-O2", str(cpp), "-o", str(dll)],
                       check=True, capture_output=True)
        cls.lib = C.CDLL(str(dll))
        cls.addClassCleanup(lambda: _ctypes.FreeLibrary(cls.lib._handle))

    def test_full_opacity_keeps_retail_glass_and_water_blending(self):
        for tint in (0, 1):
            self.assertEqual(self.lib.run(255, tint, 0, 1), 1 | (2 if tint else 0))
            self.assertEqual(self.lib.renderedAlpha(), 255)
            self.assertTrue(self.lib.nativeBlendPreserved())

    def test_transparent_ghosts_do_not_write_depth(self):
        for alpha in (64, 128, 192):
            self.assertEqual(self.lib.run(alpha, 0, 0, 1), 29)
            self.assertEqual(self.lib.renderedAlpha(), alpha)
            self.assertTrue(self.lib.ghostBlendApplied())

    def test_after_draw_and_missing_packet_state_have_no_effect(self):
        self.assertEqual(self.lib.run(128, 1, 1, 1), 0)
        self.assertEqual(self.lib.run(128, 1, 0, 0), 0)


if __name__ == "__main__":
    unittest.main()
