"""Execute the production goop-material toggle and its savestate reconciliation."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

FIXTURE = r'''
typedef unsigned int u32; typedef unsigned short u16;
typedef short s16; typedef unsigned char u8;
static u32 blockType, inspections, unlocks, rebuilds;
struct J3DTevBlock { u8 bytes[128]; u32 getType() { ++inspections; return blockType; } } block;
struct J3DMaterial { u8 bytes[64]; } material;
struct J3DModelData { u8 bytes[64]; } data;
struct Model { void unlock() { ++unlocks; } } model;
static u8 displayed[128];
static void copy(void*d,const void*s,u32 n) {u8*a=(u8*)d;const u8*b=(const u8*)s;while(n--)*a++=*b++;}
struct Actor { void resetDL() { ++rebuilds; copy(displayed,block.bytes,128); } } actor;
struct TPollutionLayer { Model*mModel; J3DModelData*mModelData; Actor*mActor; } layer;
struct Pollution { u32 mJointModelNum; TPollutionLayer**mJointModels; } pollution;
static Pollution*gpPollution=&pollution;
struct TMarDirector {} director;
static TMarDirector*gpMarDirector=&director;
static bool enabled;
enum {SETTING_VISIBLE_GOOP};
struct Settings {bool getBool(int){return enabled;}} gSettings;
static u8 pe[32], savedBlock[128], savedPe[32], savedDL[128];
static J3DMaterial* materialList[1];
static TPollutionLayer* layers[1];
'''

EXPORTS = r'''
#define API extern "C" __declspec(dllexport)
API void reset(u32 type) {
    blockType=type; inspections=unlocks=rebuilds=0; enabled=false;
    for(u32 i=0;i<128;++i)block.bytes[i]=0xA5;
    for(u32 i=0;i<32;++i)pe[i]=0xB6;
    for(u32 i=0;i<64;++i)material.bytes[i]=data.bytes[i]=0xC7;
    *(J3DTevBlock**)(material.bytes+0x28)=&block;
    *(u8**)(material.bytes+0x30)=pe;
    *(u16*)(data.bytes+0x24)=1;
    *(J3DMaterial***)(data.bytes+0x28)=materialList;materialList[0]=&material;
    layer={&model,&data,&actor};layers[0]=&layer;
    pollution={1,layers};gpPollution=&pollution;gpMarDirector=&director;
    u8*raw=block.bytes;
    const bool two=type==kTevBlock2;
    const u32 order=two?8:12,count=two?0x30:0x1C,stages=two?0x31:0x1D,color=two?0x20:0x4E;
    raw[order]=0;raw[order+1]=0;raw[order+2]=4;raw[count]=2;
    TevStage*s=(TevStage*)(raw+stages);
    s[0].colorReg=0xC0;s[0].alphaReg=0xC1;setStage(s[0],kRetailStage);
    s[1].colorReg=0xC2;s[1].alphaReg=0xC3;
    s[1].alphaOp=0x31;s[1].alphaAB=0xFF;s[1].swapMode=0x80;
    setColor2((s16*)(raw+color),false);setAlpha(*(AlphaComp*)(pe+8),kRetailAlpha);
    copy(displayed,raw,128);visibleGoopOnStageSetup();
}
API void setting(u32 on){enabled=on!=0;}
API void tick(){visibleGoopUpdate();}
API void snapshot(){copy(savedBlock,block.bytes,128);copy(savedPe,pe,32);copy(savedDL,displayed,128);}
API void restore(){copy(block.bytes,savedBlock,128);copy(pe,savedPe,32);copy(displayed,savedDL,128);visibleGoopOnSavestateLoaded();}
API void detach(u32 which){if(which==0)gpMarDirector=0;else gpPollution=0;}
API void unsupported(){block.bytes[blockType==kTevBlock2?8:12]=7;}
API u32 count(u32 which){return which==0?inspections:which==1?unlocks:rebuilds;}
API void bytes(void*out){copy(out,block.bytes,128);copy((u8*)out+128,pe,32);}
API u32 displayMatches(){for(u32 i=0;i<128;++i)if(block.bytes[i]!=displayed[i])return 0;return 1;}
'''


class VisibleGoopTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / 'toolchain/clang++.exe'
        if not compiler.exists():
            raise unittest.SkipTest('Bundled Windows compiler required')
        cls.temp = tempfile.TemporaryDirectory(prefix='moonshine-goop-')
        cls.addClassCleanup(cls.temp.cleanup)
        production = (ROOT/'src/visible_goop.cpp').read_text()
        production = '\n'.join(line for line in production.splitlines()
                               if not line.startswith('#include'))
        path = Path(cls.temp.name)/'goop.cpp'
        path.write_text(FIXTURE+production+EXPORTS)
        subprocess.run([str(compiler), '--target=x86_64-pc-windows-msvc', '-shared',
                        '-nostdlib', '-fuse-ld=lld', '-Wl,/noentry', '-O2',
                        '-fno-builtin', str(path), '-o', str(path.with_suffix('.dll'))], check=True)
        cls.lib = C.CDLL(str(path.with_suffix('.dll')))
        cls.addClassCleanup(lambda: C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))
        cls.lib.bytes.argtypes = [C.c_void_p]

    def image(self):
        out = C.create_string_buffer(160)
        self.lib.bytes(out)
        return out.raw

    def settle(self):
        for _ in range(8):
            self.lib.tick()

    def test_off_is_inert_without_even_inspecting_materials(self):
        for kind in (0x54564232, 0x54564234):
            self.lib.reset(kind)
            before = self.image()
            self.settle()
            self.assertEqual(self.image(), before)
            self.assertEqual([self.lib.count(i) for i in range(3)], [0, 0, 0])

    def test_toggle_restores_exact_retail_bytes_and_stops_work_once_settled(self):
        for kind in (0x54564232, 0x54564234):
            self.lib.reset(kind)
            before = self.image()
            self.lib.setting(1); self.settle()
            self.assertNotEqual(self.image(), before)
            self.assertEqual(self.lib.displayMatches(), 1)
            inspections = self.lib.count(0)
            self.settle()
            self.assertEqual(self.lib.count(0), inspections)
            self.lib.setting(0); self.settle()
            self.assertEqual(self.image(), before)
            self.assertEqual(self.lib.displayMatches(), 1)

    def test_loading_each_toggle_phase_reconciles_to_current_setting(self):
        for source_setting in (0, 1):
            for target_setting in (0, 1):
                for saved_frame in range(4):
                    with self.subTest(source=source_setting, target=target_setting, frame=saved_frame):
                        self.lib.reset(0x54564232)
                        retail = self.image()
                        self.lib.setting(1); self.settle()
                        revealed = self.image()
                        self.lib.setting(1-source_setting); self.settle()
                        self.lib.setting(source_setting)
                        for _ in range(saved_frame): self.lib.tick()
                        self.lib.snapshot()
                        self.lib.setting(target_setting); self.settle()
                        self.lib.restore(); self.settle()
                        self.assertEqual(self.image(), revealed if target_setting else retail)
                        self.assertEqual(self.lib.displayMatches(), 1)

    def test_unknown_materials_and_destroyed_stages_are_untouched(self):
        for change in ('unsupported', 'director', 'pollution'):
            self.lib.reset(0x54564234)
            if change == 'unsupported': self.lib.unsupported()
            else: self.lib.detach(int(change == 'pollution'))
            before = self.image()
            self.lib.setting(1); self.settle()
            self.assertEqual(self.image(), before)
            self.assertEqual(self.lib.count(1), 0)
            self.assertEqual(self.lib.count(2), 0)


if __name__ == '__main__':
    unittest.main()
