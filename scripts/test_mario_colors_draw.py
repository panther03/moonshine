"""Exercise the production draw-callback lifecycle across state restores."""

import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_native_timer_creation import function

ROOT = Path(__file__).resolve().parents[1]


class MarioDrawLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / "toolchain/clang++.exe"
        if not compiler.exists():
            raise unittest.SkipTest("Bundled Windows compiler required")
        cls.temp = tempfile.TemporaryDirectory(prefix="moonshine-mario-draw-")
        cls.addClassCleanup(cls.temp.cleanup)
        work = Path(cls.temp.name)
        source = (ROOT / "src/mario_colors_draw.cpp").read_text()
        shared = (ROOT / "include/susamune/model_color_draw.hxx").read_text()
        structs = source[source.index("typedef void (*PacketCallback)"):
                         source.index("static_assert(")]
        code = r'''
typedef unsigned char u8;typedef unsigned short u16;typedef unsigned u32;typedef short s16;
extern "C" void *memset(void*d,int v,unsigned long long n){u8*p=(u8*)d;while(n--)*p++=(u8)v;return d;}
struct J3DShapePacket{u8 bytes[0x34];};
struct ResTIMG{unsigned mTextureOffset;};
struct J3DModelData{u16 mShapeNum;};
struct J3DModel{J3DModelData*mModelData;J3DShapePacket*mShapePackets;};
struct M3UModel{J3DModel*mModel;};
struct TMarioCap{J3DModel*mCap1,*maGlass1;};
struct TMario{M3UModel*mModelData;TMarioCap*mCap;J3DModel*mHandModel2R,*mHandModel2L,*mHandModel3R,*mHandModel3L,*mHandModel4R;};
struct TMarDirector{enum{STATE_GAME_STARTING=2};int _260,mCurState;};
struct TApplication{enum{CONTEXT_DIRECT_STAGE=5};int mContext;}gpApplication;
TMarDirector*gpMarDirector;TMario*gpMarioAddress;
namespace RetailInput {TMarDirector*stageDirector(){return gpApplication.mContext==5?gpMarDirector:nullptr;}}
struct GXTexObj{int unused;};struct GXColorS10{s16 r,g,b,a;};
enum{GX_TEVREG2,GX_TEVSTAGE0,GX_TEVSTAGE1,GX_CC_C2,GX_CC_ONE,GX_CC_TEXC,GX_CC_ZERO,GX_CC_CPREV,GX_CC_C0,GX_CC_C1,GX_CC_RASC,GX_CC_KONST,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_CS_SCALE_2,GX_TRUE,GX_TEVPREV,GX_TEXMAP0};
int customLoads,retailCalls,otherCalls;
void GXSetTevColorS10(int,GXColorS10){}void GXSetTevColorIn(int,int,int,int,int){}
void GXSetTevColorOp(int,int,int,int,int,int){}void GXLoadTexObj(GXTexObj*,int){customLoads++;}
void DCStoreRange(void*,unsigned){}void GXInvalidateTexAll(){}
namespace MarioColorTexture {const unsigned kAtlasBytes=32768;void recolor(const u8*,u8*,const u8(*)[3],u8){}}
'''
        code += "namespace ModelColorDraw {bool mem1(const void*p,unsigned){return p!=0;}\n"
        code += function(shared, "live") + function(shared, "supportedModel")
        code += "void initTexture(GXTexObj&,const ResTIMG&,const void*,u16,u16){} }\n"
        code += r'''
namespace MarioColors{
enum{CAP,SHIRT,OVERALLS,GLOVES,SHOES,SUNGLASSES,SUNSHINE_SHIRT,PART_COUNT};
unsigned enabledMask;u8 color[3]={255,255,255};
bool enabled(unsigned p){return enabledMask&(1u<<p);}const u8*rgb(unsigned){return color;}
'''
        code += structs
        code += r'''
using ModelColorDraw::mem1;
using ModelColorDraw::supportedModel;
ResTIMG texture={};const ResTIMG*mainTexture(J3DModelData*){return &texture;}
void initTexture(GXTexObj&,const ResTIMG&,const void*){}
'''
        code += "\n".join(function(source, n) for n in (
            "live", "callback", "drawPacket", "install",
            "onStageSetup", "update"))
        code += r'''
J3DShapePacket packets[15];J3DModelData data[8];J3DModel models[8];
M3UModel modelOwner;TMarioCap cap;TMario mario;TMarDirector director;
void retail(J3DShapePacket*,int){retailCalls++;}
void other(J3DShapePacket*,int){otherCalls++;}
void initialize(){
 memset(packets,0,sizeof(packets));memset(&mario,0,sizeof(mario));
 // The body needs eleven packets; optional models are absent in this fixture.
 data[0].mShapeNum=11;models[0]={&data[0],packets};modelOwner={&models[0]};
 mario.mModelData=&modelOwner;director={1,2};gpMarDirector=&director;gpMarioAddress=&mario;
 gpApplication.mContext=TApplication::CONTEXT_DIRECT_STAGE;enabledMask=0;
 retailCalls=otherCalls=customLoads=0;
 for(unsigned i=0;i<11;i++)callback(&packets[i])=retail;
 onStageSetup();
}
extern "C" __declspec(dllexport) int run(int scenario){
 initialize();update();
 if(sDraw.packetCount!=7||callback(&packets[4])!=drawPacket)return 1;
 if(scenario==0){
  // A fresh boot with Original selected must know originals before loading a wrapped state.
  callback(&packets[4])=drawPacket;update();enabledMask=1u<<CAP;update();
  callback(&packets[4])(&packets[4],0);callback(&packets[4])(&packets[4],1);
  if(retailCalls!=2||customLoads!=2)return 2;
  enabledMask=0;update();callback(&packets[4])(&packets[4],0);
  if(retailCalls!=3||customLoads!=2)return 3;
 }else if(scenario==1){
  // A restored retail callback replaces the registry's previous chain target.
  callback(&packets[4])=other;update();callback(&packets[4])(&packets[4],0);
  if(retailCalls||otherCalls!=1)return 4;
 }else if(scenario==2){
  // A newly allocated stage reusing addresses must capture that stage's originals.
  for(unsigned i=0;i<11;i++)callback(&packets[i])=other;
  onStageSetup();update();callback(&packets[4])(&packets[4],0);
  if(retailCalls||otherCalls!=1||sDraw.packetCount!=7)return 5;
 }else{
  enabledMask=1u<<CAP;update();gpApplication.mContext=0;
  callback(&packets[4])(&packets[4],0);
  if(retailCalls!=1||customLoads)return 6;
 }
 return 0;
}
}
'''
        cpp = work / "draw.cpp"
        cpp.write_text(code)
        dll = work / "draw.dll"
        subprocess.run([str(compiler), "--target=x86_64-pc-windows-msvc", "-shared",
                        "-nostdlib", "-fno-builtin", "-fuse-ld=lld", "-Wl,/noentry",
                        str(cpp), "-o", str(dll)], check=True)
        cls.lib = C.CDLL(str(dll))
        cls.addClassCleanup(lambda: C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def test_disabled_fresh_stage_then_wrapped_restore_and_enable(self):
        self.assertEqual(self.lib.run(0), 0)

    def test_loaded_retail_callback_becomes_chain_target(self):
        self.assertEqual(self.lib.run(1), 0)

    def test_new_stage_reusing_addresses_rebinds_retail_callback(self):
        self.assertEqual(self.lib.run(2), 0)

    def test_stale_stage_draw_only_chains_retail(self):
        self.assertEqual(self.lib.run(3), 0)


if __name__ == "__main__":
    unittest.main()
