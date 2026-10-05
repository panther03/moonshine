"""Exercise the production draw scopes without retail art or a GPU."""

import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_native_timer_creation import function

ROOT = Path(__file__).resolve().parents[1]


class WaterColorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / "toolchain/clang++.exe"
        if not compiler.exists():
            raise unittest.SkipTest("Bundled Windows compiler required")
        cls.temp = tempfile.TemporaryDirectory(prefix="moonshine-water-colours-")
        cls.addClassCleanup(cls.temp.cleanup)
        work = Path(cls.temp.name)
        source = (ROOT / "src/water_colors.cpp").read_text()
        code = r'''
typedef unsigned char u8;typedef unsigned u32;
extern "C" void *memset(void*d,int v,unsigned long long n){u8*p=(u8*)d;while(n--)*p++=(u8)v;return d;}
struct GXColor{u8 r,g,b,a;};namespace JDrama{struct TGraphics{};}
namespace GhostFludd {void draw(JDrama::TGraphics*);}
struct TWaterGun{u8 data[0x1dbc];};struct TMario{TWaterGun*mFludd;};
struct TMarDirector{enum{STATE_GAME_STARTING=2};int _260,mCurState;};
struct TApplication{enum{CONTEXT_DIRECT_STAGE=5};int mContext;}gpApplication;
TMario*gpMarioAddress;TMarDirector*gpMarDirector;TMarDirector director;
namespace RetailInput {TMarDirector*stageDirector(){return gpApplication.mContext==5?gpMarDirector:nullptr;}}
void *gpModelWaterManager,*gpSplashManager,*gpMarioParticleManager;
GXColor gModelWaterManagerWaterColor[4];void *waterVtable[9],*splashVtable[9],*particleVtable[9];
namespace FluddColors{enum{WATER=8,WATER_HIGHLIGHT=9};unsigned mask;u8 colors[2][3];
 bool enabled(unsigned p){return mask&(1u<<p);}const u8*rgb(unsigned p){return colors[p-8];}}
void retailWater(void*,u32,JDrama::TGraphics*);void retailSplash(void*,u32,JDrama::TGraphics*);
void retailParticles(void*,u32,JDrama::TGraphics*);void DCStoreRange(void*,unsigned){}
namespace WaterColors{
typedef void(*Perform)(void*,u32,JDrama::TGraphics*);TMarDirector*sDirector;
const u32 kDraw=8,kWaterDraw=0x88,kMistCount=32;
bool mem1(const void*p,u32 n){auto a=(__UINTPTR_TYPE__)p;return a>=0x81000000u&&a+n<=0x81100000u;}
'''
        code += "\n".join(function(source, n) for n in (
            "live", "color", "pointerAt", "replace", "custom", "normalWater",
            "drawWater", "drawSplash"))
        code += "struct MistColor {void*emitter;GXColor primary,environment;};\n"
        code += "\n".join(function(source, n) for n in ("drawParticles", "install", "onStageSetup"))
        code += r'''
}
using namespace WaterColors;
unsigned calls,cues[4],currentScenario;GXColor observed[4][7];
unsigned ghostCalls,retailBeforeGhost;GXColor ghostPalette;
void GhostFludd::draw(JDrama::TGraphics*){
 ++ghostCalls;retailBeforeGhost=calls;ghostPalette=gModelWaterManagerWaterColor[0];
}
u8*base;void*e1;void*e2;void*e3;u8*infos;
bool equal(GXColor a,GXColor b){return a.r==b.r&&a.g==b.g&&a.b==b.b&&a.a==b.a;}
void pointer(void*p,unsigned off,void*v){*(u32*)((u8*)p+off)=(u32)(__UINTPTR_TYPE__)v;}
void observe(u32 cue){
 unsigned i=calls++;cues[i]=cue;
 observed[i][0]=gModelWaterManagerWaterColor[0];observed[i][1]=color(gpModelWaterManager,0x5d20);
 observed[i][2]=color(gpModelWaterManager,0x5d24);observed[i][3]=color(gpSplashManager,0x63c);
 observed[i][4]=color(e1,0x180);observed[i][5]=color(e2,0x180);observed[i][6]=color(e3,0x180);
}
void retailWater(void*,u32 cue,JDrama::TGraphics*){observe(cue);}
void retailSplash(void*,u32 cue,JDrama::TGraphics*){observe(cue);}
void retailParticles(void*,u32 cue,JDrama::TGraphics*){
 observe(cue);if(currentScenario==5&&(cue&2))pointer(infos,12,e2);
}
void initialize(void*memory,unsigned scenario){
 base=(u8*)memory;memset(base,0,0x30000);currentScenario=scenario;calls=ghostCalls=retailBeforeGhost=0;
 gpModelWaterManager=base;gpSplashManager=base+0x10000;gpMarioParticleManager=base+0x12000;
 gpMarioAddress=(TMario*)(base+0x20000);gpMarioAddress->mFludd=(TWaterGun*)(base+0x21000);
 director={1,2};gpMarDirector=&director;gpApplication.mContext=5;
 FluddColors::mask=0;FluddColors::colors[0][0]=19;FluddColors::colors[0][1]=61;FluddColors::colors[0][2]=255;
 FluddColors::colors[1][0]=255;FluddColors::colors[1][1]=0;FluddColors::colors[1][2]=0;
 gModelWaterManagerWaterColor[0]={60,70,120,20};gModelWaterManagerWaterColor[1]={254,168,2,110};
 color(gpModelWaterManager,0x5d20)={188,204,220,255};color(gpModelWaterManager,0x5d24)={142,142,158,215};
 color(gpSplashManager,0x63c)={168,203,227,171};
 e1=base+0x23000;e2=base+0x24000;e3=base+0x25000;infos=base+0x26000;
 pointer(gpMarioParticleManager,0x50,infos);pointer(gpMarioParticleManager,0x3b8,base+0x27000);
 *(u32*)((u8*)gpMarioParticleManager+0x3b4)=32;
 for(unsigned i=0;i<32;i++)pointer(infos+i*16,0,gpMarioAddress->mFludd);
 pointer(infos,12,e1);pointer(infos+16,12,e1);pointer(infos+32,12,e2);pointer(infos+48,12,e3);
 pointer(infos+32,0,base+0x11000);
 for(void*e:{e1,e2,e3}){pointer(e,0x10c,base+0x27000);color(e,0x180)={255,251,245,91};color(e,0x184)={255,255,255,17};}
 pointer(e3,0x10c,base+0x28000);
 waterVtable[8]=(void*)retailWater;splashVtable[8]=(void*)retailSplash;particleVtable[8]=(void*)retailParticles;
 onStageSetup();
}
extern "C" __declspec(dllexport) int run(void*memory,unsigned scenario){
 initialize(memory,scenario);
 const GXColor baseColor=gModelWaterManagerWaterColor[0],juice=gModelWaterManagerWaterColor[1];
 const GXColor shine=color(gpModelWaterManager,0x5d20),shade=color(gpModelWaterManager,0x5d24);
 const GXColor spray=color(gpSplashManager,0x63c),mist=color(e1,0x180),env=color(e1,0x184);
 const GXColor customWater={19,61,255,20};
 if(scenario==0){
  drawWater(gpModelWaterManager,8,0);drawSplash(gpSplashManager,8,0);drawParticles(gpMarioParticleManager,0x80000008,0);
  if(calls!=3||!equal(observed[0][0],baseColor)||!equal(observed[1][3],spray)||!equal(observed[2][4],mist))return 1;
 }else if(scenario==1||scenario==2){
  FluddColors::mask=0x300;drawWater(gpModelWaterManager,scenario==1?8:0x8d,0);
  unsigned i=scenario==1?0:1;
  if(calls!=i+1||!equal(observed[i][0],customWater)||!equal(observed[i][1],{220,0,0,255})||!equal(observed[i][2],{158,0,0,215}))return 2;
  if(i&&(cues[0]!=5||cues[1]!=0x88||!equal(observed[0][0],baseColor)))return 3;
 }else if(scenario==3){
  FluddColors::mask=0x300;((u8*)gpModelWaterManager)[0x5d5f]=1;
  drawWater(gpModelWaterManager,8,0);drawSplash(gpSplashManager,8,0);drawParticles(gpMarioParticleManager,8,0);
  if(calls!=3||!equal(observed[0][0],baseColor)||!equal(observed[1][3],spray)||!equal(observed[2][4],mist))return 4;
 }else if(scenario==4){
  FluddColors::mask=0x300;drawParticles(gpMarioParticleManager,0x8000000a,0);
  if(calls!=2||cues[0]!=0x80000002||cues[1]!=0x80000008||!equal(observed[0][4],mist)||!equal(observed[1][4],{19,61,255,91})||!equal(observed[1][5],mist)||!equal(observed[1][6],mist))return 5;
 }else if(scenario==5){
  FluddColors::mask=0x300;*(u32*)((u8*)gpMarioParticleManager+0x3b4)=1;
  drawParticles(gpMarioParticleManager,0x8000000a,0);
  if(calls!=2||!equal(observed[1][4],mist)||!equal(observed[1][5],{19,61,255,91}))return 6;
 }else if(scenario==6){
  FluddColors::mask=0x300;*(u32*)((u8*)gpMarioParticleManager+0x3b4)=33;
  drawParticles(gpMarioParticleManager,8,0);
  if(calls!=1||!equal(observed[0][4],mist))return 7;
 }else if(scenario==7){
  FluddColors::mask=0x300;drawSplash(gpSplashManager,0xc,0);
  if(calls!=2||!equal(observed[0][3],spray)||!equal(observed[1][3],{19,61,255,171}))return 8;
 }else if(scenario==9){
  FluddColors::mask=0x300;drawWater(gpModelWaterManager,0x80,0);
  if(ghostCalls!=1||retailBeforeGhost!=1||!equal(ghostPalette,baseColor))return 13;
  drawWater(gpModelWaterManager,8,0);
  if(ghostCalls!=1)return 14;
  gpApplication.mContext=0;drawWater(gpModelWaterManager,0x80,0);
  if(ghostCalls!=1)return 15;
 }else{
  if(waterVtable[8]!=(void*)drawWater||splashVtable[8]!=(void*)drawSplash||particleVtable[8]!=(void*)drawParticles)return 9;
  splashVtable[8]=(void*)retailWater;onStageSetup();if(splashVtable[8]!=(void*)retailWater)return 10;
  FluddColors::mask=0x300;gpApplication.mContext=0;drawWater(gpModelWaterManager,8,0);
  if(calls!=1||!equal(observed[0][0],baseColor))return 11;
 }
 if(!equal(gModelWaterManagerWaterColor[0],baseColor)||!equal(gModelWaterManagerWaterColor[1],juice)||!equal(color(gpModelWaterManager,0x5d20),shine)||!equal(color(gpModelWaterManager,0x5d24),shade)||!equal(color(gpSplashManager,0x63c),spray)||!equal(color(e1,0x180),mist)||!equal(color(e2,0x180),mist)||!equal(color(e3,0x180),mist)||!equal(color(e1,0x184),env)||!equal(color(e2,0x184),env))return 12;
 return 0;
}
'''
        code = code.replace("for(void*e:{e1,e2,e3})", "void*emitters[]={e1,e2,e3};for(void*e:emitters)")
        cpp = work / "water.cpp"
        cpp.write_text(code)
        dll = work / "water.dll"
        subprocess.run([str(compiler), "--target=x86_64-pc-windows-msvc", "-shared",
                        "-nostdlib", "-fno-builtin", "-fuse-ld=lld", "-Wl,/noentry",
                        str(cpp), "-o", str(dll)], check=True)
        cls.lib = C.CDLL(str(dll))
        cls.addClassCleanup(lambda: C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))
        cls.lib.run.argtypes = [C.c_void_p, C.c_uint]
        kernel = C.windll.kernel32
        kernel.VirtualAlloc.restype = C.c_void_p
        kernel.VirtualAlloc.argtypes = [C.c_void_p, C.c_size_t, C.c_uint, C.c_uint]
        cls.memory = kernel.VirtualAlloc(C.c_void_p(0x81000000), 0x100000, 0x3000, 4)
        if cls.memory != 0x81000000:
            raise unittest.SkipTest("Test MEM1 address unavailable")
        cls.addClassCleanup(lambda: kernel.VirtualFree(C.c_void_p(cls.memory), 0, 0x8000))

    def test_original_preserves_all_three_renderers(self):
        self.assertEqual(self.lib.run(self.memory, 0), 0)

    def test_water_and_highlights_restore_exact_rgba(self):
        self.assertEqual(self.lib.run(self.memory, 1), 0)

    def test_mixed_gameplay_and_draw_cues_only_lend_draw_palette(self):
        self.assertEqual(self.lib.run(self.memory, 2), 0)

    def test_yoshi_juice_retains_its_palette_and_effects(self):
        self.assertEqual(self.lib.run(self.memory, 3), 0)

    def test_mist_restricts_owners_and_engine_and_deduplicates(self):
        self.assertEqual(self.lib.run(self.memory, 4), 0)

    def test_mist_collects_after_particle_engine_replaces_emitter(self):
        self.assertEqual(self.lib.run(self.memory, 5), 0)

    def test_invalid_mist_capacity_does_not_read_slots(self):
        self.assertEqual(self.lib.run(self.memory, 6), 0)

    def test_splash_calculation_and_draw_are_separate(self):
        self.assertEqual(self.lib.run(self.memory, 7), 0)

    def test_repeat_stage_install_preserves_foreign_hooks_and_stale_guard(self):
        self.assertEqual(self.lib.run(self.memory, 8), 0)

    def test_ghost_water_follows_composite_with_retail_palette_restored(self):
        self.assertEqual(self.lib.run(self.memory, 9), 0)


if __name__ == "__main__":
    unittest.main()
