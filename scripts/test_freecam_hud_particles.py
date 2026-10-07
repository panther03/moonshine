"""Cinematic HUD suppression includes the separate retail 2D particle pass."""
import ctypes as C
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_practice_tape import function_source

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT/'src/gameplay_polish.cpp'
PRACTICE = ROOT/'src/practice_session.cpp'


class HudParticleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='moonshine-hud-particles-')
        cls.addClassCleanup(cls.temp.cleanup)
        code = r'''
using u8=unsigned char;using u32=unsigned;
namespace JDrama {struct TViewObj {u8 bytes[16];};struct TGraphics {u32 value;};}
struct View {JDrama::TViewObj base;void *manager;};
struct Emitter {u32 age,draws;} hud,world;
View hudView,worldView;JDrama::TGraphics graphics={0x1234};
bool sCinemaHidden;void *gpEmitterManager4D2;u32 calls,lastCue,wrong;
void susamuneRetailHudParticles(JDrama::TViewObj *view,u32 cue,JDrama::TGraphics *gfx){
 ++calls;lastCue=cue;wrong|=gfx!=&graphics;
 View*v=reinterpret_cast<View*>(view);
 Emitter*m=static_cast<Emitter*>(v->manager);
 if(cue&2)++m->age;if(cue&8)++m->draws;
}
'''
        code += function_source(SOURCE, 'void susamunePracticeHudParticles(')
        code += r'''
extern "C" {
__declspec(dllexport) void reset(){
 hud={};world={};calls=lastCue=wrong=0;sCinemaHidden=false;
 hudView.manager=&hud;worldView.manager=&world;gpEmitterManager4D2=&hud;
 for(u32 i=0;i<16;++i)hudView.base.bytes[i]=0x5a;
}
__declspec(dllexport) void dispatch(u32 cue,u32 hide,u32 isHud,u32 live){
 sCinemaHidden=hide;gpEmitterManager4D2=live?&hud:nullptr;
 susamunePracticeHudParticles(isHud?&hudView.base:&worldView.base,cue,&graphics);
}
__declspec(dllexport) u32 get(u32 key){switch(key){
 case 0:return calls;case 1:return lastCue;case 2:return hud.age;
 case 3:return hud.draws;case 4:return world.age;case 5:return world.draws;
 case 6:return wrong;case 7:{for(u32 i=0;i<16;++i)if(hudView.base.bytes[i]!=0x5a)return 0;return 1;}}
 return 0;}
}
'''
        path=Path(cls.temp.name)/'particles.cpp';path.write_text(code)
        subprocess.run([str(ROOT/'toolchain/clang++.exe'), '--target=x86_64-pc-windows-msvc',
            '-shared','-nostdlib','-fuse-ld=lld','-Wl,/noentry','-O2',str(path),
            '-o',str(path.with_suffix('.dll'))],capture_output=True,text=True,check=True)
        cls.lib=C.CDLL(str(path.with_suffix('.dll')))
        cls.lib.get.restype=C.c_uint
        cls.addClassCleanup(lambda:C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def setUp(self):self.lib.reset()

    def test_live_hud_sparkles_keep_aging_while_hidden(self):
        for _ in range(60):self.lib.dispatch(0xb,1,1,1)
        self.assertEqual([self.lib.get(i) for i in range(8)], [60,3,60,0,0,0,0,1])

    def test_every_other_cue_is_preserved_and_retail_runs_once(self):
        for cue in (0,1,2,3,8,0xb,0x40000008,0xffffffff):
            self.lib.reset();self.lib.dispatch(cue,1,1,1)
            self.assertEqual((self.lib.get(0),self.lib.get(1)),(1,cue&~8))

    def test_world_particles_and_missing_hud_owner_pass_through(self):
        for hide,isHud,live in ((1,0,1),(0,1,1),(1,1,0)):
            self.lib.reset();self.lib.dispatch(0xb,hide,isHud,live)
            self.assertEqual((self.lib.get(0),self.lib.get(1)),(1,0xb))
            self.assertEqual(self.lib.get(3 if isHud else 5),1)

    def test_reenabling_hud_restores_draw_without_restarting_emitter(self):
        self.lib.dispatch(0xb,1,1,1);self.lib.dispatch(0xb,0,1,1)
        self.assertEqual((self.lib.get(2),self.lib.get(3)),(2,1))

    def test_hook_uses_checked_regional_retail_vtable_slot(self):
        text=PRACTICE.read_text()
        init=function_source(PRACTICE,'void init()')
        self.assertIn('installVtableEntry(reinterpret_cast<u32 *>(kHudEmitterVtable + 0x20u)',init)
        for key,symbol in (('kHudEmitterVtable','__vt__15TEmitterViewObj'),
                           ('kHudEmitterPerform','perform__15TEmitterViewObjFUlPQ26JDrama9TGraphics')):
            values=re.search(rf'{key} = SUSAMUNE_MEM1_ADDR\((.*?)\);',text).group(1)
            values=[int(v.strip().rstrip('u'),16) for v in values.split(',')]
            for region,value in zip(('jp','us','pal'),values):
                mapped=re.search(rf'^{symbol} = (0x[0-9a-fA-F]+);',
                    (ROOT/f'maps/{region}.ld').read_text(),re.M)
                self.assertEqual(value,int(mapped.group(1),16))


if __name__=='__main__':unittest.main()
