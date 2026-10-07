"""Execute ghost environment routing and timeline-only pump pose selection."""
import ctypes as C
import _ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_native_timer_creation import function

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'src/ghost_model.cpp').read_text()

class GhostLightingAnimationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / 'toolchain/clang++.exe'
        if not compiler.exists(): raise unittest.SkipTest('Bundled compiler required')
        cls.temp = tempfile.TemporaryDirectory(prefix='moonshine-ghost-light-')
        cls.addClassCleanup(cls.temp.cleanup)
        root = Path(cls.temp.name)
        code = r'''
extern "C" int _fltused=0;
typedef unsigned char u8; typedef unsigned short u16; typedef short s16;
typedef unsigned u32; typedef float f32;
struct TVec3f {float x,y,z;}; typedef TVec3f Vec;
namespace Ghost {struct VisualState {float x,y,z;unsigned visualQf;};}
struct TBGCheckData {unsigned short mType;short mValue;};
TBGCheckData floorData; bool hasFloor; float floorY;
struct Map {float checkGround(float,float,float,const TBGCheckData **p){*p=hasFloor?&floorData:nullptr;return floorY;}} map;
struct Cube {u32 getInCubeNo(const Vec &){return cube;}; u32 cube;} cube;
Map *gpMap=&map; Cube *gpCubeShadow=&cube;
u8 manager[64], lightSet[64]; void *sets[4]; void *gpLightManager=manager;
int selected=-1, entries;
void ghostLightEntry(void *,int i){selected=i;++entries;}
struct GXColor {u8 r,g,b,a;}; GXColor sGhostColor, drawnColor;
const int GX_COLOR0A0=4;const int kShapePacketUserAreaOffset=12;
struct J3DShapePacket{u8 data[32];};
void GXSetChanMatColor(int,GXColor color){drawnColor=color;}

'''
        code += function(SOURCE, 'lightSetFor') + function(SOURCE, 'pumpFrame') + function(SOURCE, 'bodyPacketCallback')
        code += r'''
extern "C" __declspec(dllexport) int route(int available,int has,int shadow,int value,float floor,float y,int inCube){
 gpLightManager=available==0?nullptr:manager;gpMap=&map;sets[1]=lightSet;
 *reinterpret_cast<void***>(manager+0x14)=sets;
 *reinterpret_cast<void**>(lightSet+0x10)=available==2?nullptr:manager;
 *reinterpret_cast<int*>(lightSet+0x1c)=2;lightSet[0x20]=available==3?0:1;
 hasFloor=has;floorData.mType=shadow?0x4000:0;floorData.mValue=(short)value;floorY=floor;
 cube.cube=inCube?0:0xffffffffu;entries=0;selected=-1;
 Ghost::VisualState s={20,y,50,0};lightSetFor(s);return selected;
}
extern "C" __declspec(dllexport) unsigned shade(unsigned rgba,unsigned alpha,int stage){
 GXColor original={(u8)(rgba>>24),(u8)(rgba>>16),(u8)(rgba>>8),(u8)rgba};
 J3DShapePacket packet;*reinterpret_cast<const GXColor**>(packet.data+12)=&original;
 sGhostColor={255,255,255,(u8)alpha};drawnColor={1,2,3,4};bodyPacketCallback(&packet,stage);
 return ((unsigned)drawnColor.r<<24)|((unsigned)drawnColor.g<<16)|((unsigned)drawnColor.b<<8)|drawnColor.a;
}
extern "C" __declspec(dllexport) float phase(unsigned qf,short end){Ghost::VisualState s={0,0,0,qf};return pumpFrame(s,end);}
'''
        (root/'test.cpp').write_text(code)
        subprocess.run([str(compiler),'--target=x86_64-pc-windows-msvc','-shared','-nostdlib','-fno-builtin','-fuse-ld=lld','-Wl,/noentry','-O2',str(root/'test.cpp'),'-o',str(root/'test.dll')],check=True)
        cls.lib=C.CDLL(str(root/'test.dll'));cls.addClassCleanup(lambda:_ctypes.FreeLibrary(cls.lib._handle))
        cls.lib.route.argtypes=[C.c_int]*4+[C.c_float]*2+[C.c_int]
        cls.lib.shade.argtypes=[C.c_uint,C.c_uint,C.c_int];cls.lib.shade.restype=C.c_uint
        cls.lib.phase.argtypes=[C.c_uint,C.c_short];cls.lib.phase.restype=C.c_float

    def test_sun_and_ground_shadow_follow_ghost_position(self):
        self.assertEqual(self.lib.route(1,1,0,1,0,0,0),0)
        self.assertEqual(self.lib.route(1,1,1,1,0,0,0),1)
        self.assertEqual(self.lib.route(1,1,1,1,0,250,0),0)

    def test_cube_shadow_overrides_air_and_missing_floor(self):
        self.assertEqual(self.lib.route(1,0,0,0,-9999,400,1),1)

    def test_absent_stage_lighting_is_not_enabled_or_allocated(self):
        for available in (0,2,3):self.assertEqual(self.lib.route(available,1,1,1,0,0,0),-1)

    def test_bad_floor_light_index_is_bounded_before_retail_call(self):
        for index in (-1,2,100):self.assertEqual(self.lib.route(1,1,1,index,0,0,0),0)

    def test_pause_seek_and_wrap_pose_depend_only_on_recording_time(self):
        self.assertEqual([self.lib.phase(q,20) for q in (0,20,40,80,120,160)], [0,5,10,20,10,0])
        self.assertEqual(self.lib.phase(40,20),self.lib.phase(40,20))
        self.assertEqual(self.lib.phase(40,0),0)

    def test_actual_packet_callback_preserves_rgb_at_every_opacity(self):
        for rgb in (0x122334ff,0xa0408090,0xffffff7f):
            for alpha in (64,128,192,255):
                self.assertEqual(self.lib.shade(rgb,alpha,0),(rgb & 0xffffff00)|alpha)
        self.assertEqual(self.lib.shade(0x122334ff,192,1),0x01020304)

    def test_opacity_does_not_replace_body_material_rgb(self):
        self.assertIn('GXColor color = *original;', SOURCE)
        self.assertIn('color.a = sGhostColor.a;', SOURCE)
        self.assertNotIn('SMS_InitPacket_MatColor(', SOURCE)
        self.assertIn('if (lighting) ghostLightExit(lighting);', SOURCE)

    def test_upper_body_is_scoped_and_borrowed_animation_restored(self):
        body=function(SOURCE,'prepareRunner')
        self.assertIn('!state.yoshi',body)
        self.assertIn('SUSAMUNE_GHOST_FLUDD_SPRAYING',body)
        self.assertIn('getIndex("chn_chest")',body)
        self.assertIn('*animationFrame(upper) = savedUpperFrame',body)
        anim=function(SOURCE,'animateFludd')
        self.assertIn('count > 16',anim)
        self.assertIn('data->getJointNum()',anim)
        self.assertNotIn('->setBck',anim)

if __name__=='__main__':unittest.main()
