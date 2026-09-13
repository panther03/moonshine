"""Run typed render-binding preservation against guarded live fixtures."""

import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_native_timer_creation import function

ROOT = Path(__file__).resolve().parents[1]


class SavestateRenderBindingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / "toolchain/clang++.exe"
        if not compiler.exists():
            raise unittest.SkipTest("Bundled Windows compiler required")
        cls.temp = tempfile.TemporaryDirectory(prefix="moonshine-render-bindings-")
        cls.addClassCleanup(cls.temp.cleanup)
        work = Path(cls.temp.name)
        code = r'''
typedef unsigned char u8; typedef unsigned u32;
#define API extern "C" __declspec(dllexport)
extern "C" void *memset(void*d,int v,unsigned long long n){u8*p=(u8*)d;while(n--)*p++=(u8)v;return d;}
extern "C" void *memcpy(void*d,const void*s,unsigned long long n){u8*a=(u8*)d;const u8*b=(const u8*)s;while(n--)*a++=*b++;return d;}
struct J3DShapePacket {alignas(8) u8 bytes[0x38];};
struct TMario {};
struct TMarDirector {enum {STATE_GAME_STARTING=2};int _260,mCurState;};
TMarDirector director={1,2},otherDirector={1,2},*gpMarDirector=&director;
TMario mario,otherMario,*gpMarioAddress=&mario;
bool context=true;
namespace RetailInput {TMarDirector*stageDirector(){return context?gpMarDirector:nullptr;}}
bool mem1(const void*p,unsigned){return p && p!=(void*)1;}
const void*kept[32];unsigned long long values[32];unsigned keptCount,failAt;
bool keepWord(const void*p){if(keptCount==failAt)return false;kept[keptCount]=p;values[keptCount++]=*(const unsigned long long*)p;return true;}
void clearKept(){keptCount=0;failAt=32;}
void restoreKept(){for(unsigned i=0;i<keptCount;i++)*(unsigned long long*)kept[i]=values[i];}
'''
        for namespace, filename, count in (
            ("MarioColors", "mario_colors_draw.cpp", "packetCount"),
            ("FluddColors", "fludd_colors_draw.cpp", "count"),
        ):
            source = (ROOT / "src" / filename).read_text()
            code += "namespace " + namespace + " {\n"
            code += r'''
typedef void (*PacketCallback)(J3DShapePacket*,int);
void drawPacket(J3DShapePacket*,int){} void oldPacket(J3DShapePacket*,int){}
struct Packet {J3DShapePacket*packet;};
'''
            code += ("struct State {TMarDirector*director;TMario*mario;"
                     f"Packet packets[17];u8 {count};" + "}sDraw;\n")
            code += "\n".join(function(source, name) for name in (
                "live", "callback", "preserveSavestateBindings"))
            code += r'''
J3DShapePacket packets[3];
int run(unsigned scenario){
 gpMarDirector=&director;gpMarioAddress=&mario;context=true;director={1,2};
 sDraw={};sDraw.director=&director;sDraw.mario=&mario;
'''
            code += f"sDraw.{count}=3;\n"
            code += r'''
 clearKept();
 for(unsigned i=0;i<3;i++){sDraw.packets[i].packet=&packets[i];memset(&packets[i],0x5a,sizeof(packets[i]));callback(&packets[i])=drawPacket;}
 if(scenario==0){
  if(!preserveSavestateBindings(keepWord)||keptCount!=3)return 1;
  for(unsigned i=0;i<3;i++){if(kept[i]!=packets[i].bytes+0x10)return 2;callback(&packets[i])=oldPacket;}
  restoreKept();
  for(unsigned i=0;i<3;i++)if(callback(&packets[i])!=drawPacket||packets[i].bytes[0x0f]!=0x5a||packets[i].bytes[0x18]!=0x5a)return 3;
  return 0;
 }
 if(scenario==1)return preserveSavestateBindings(nullptr)||keptCount?4:0;
 if(scenario==2)gpMarDirector=&otherDirector;
 if(scenario==3)gpMarioAddress=&otherMario;
 if(scenario==4)context=false;
 if(scenario==5)director._260=0;
 if(scenario==6)director.mCurState=0;
 if(scenario==7)sDraw.packets[0].packet=nullptr;
 if(scenario==8)callback(&packets[0])=oldPacket;
 if(scenario==9)failAt=0;
'''
            code += f"if(scenario==10)sDraw.{count}=0;\nif(scenario==11)sDraw.{count}=17;\n"
            code += "return preserveSavestateBindings(keepWord)||keptCount?5:0;}\n}\n"
            code += f"API int {namespace}Run(unsigned scenario){{return {namespace}::run(scenario);}}\n"
        ghost = (ROOT / "src/ghost_model.cpp").read_text()
        code += r'''
namespace GhostModel {
struct Buffer {unsigned count;void frameInit(){++count;}}a,b;
struct Player {Buffer*mDrawBufferA,*mDrawBufferB;}player={&a,&b},*gpMarioOriginal=&player;
bool sRegistered,sSubmitted[2],sPrepared[2];void*sPreparedAttachments[2];
TMarDirector*sPendingDirector;u32 sLoadedGeneration,sPendingGeneration;
void*sView;void**sViewWord;
'''
        # Keep the host address full-width; the real PPC pointer is 32 bits.
        preserve = function(ghost, "preserveSavestateBindings").replace(
            "const u32 address = reinterpret_cast<u32>(sViewWord);",
            "const auto address = reinterpret_cast<__UINTPTR_TYPE__>(sViewWord);",
        )
        code += preserve + function(ghost, "retirePlayerDrawBuffers")
        code += function(ghost, "onSavestateLoaded")
        code += r'''
API int ghostRun(unsigned scenario,void**word){
 gpMarDirector=&director;director={1,2};sPendingDirector=&director;
 sLoadedGeneration=sPendingGeneration=2;sRegistered=true;
 sView=(void*)0x1234;sViewWord=word;*word=sView;clearKept();
 if(scenario==0){
  if(!preserveSavestateBindings(keepWord)||keptCount!=1||kept[0]!=word)return 1;
  *word=(void*)0x5678;restoreKept();return *word==sView?0:2;
 }
 if(scenario==1)return preserveSavestateBindings(nullptr)||keptCount?3:0;
 if(scenario==2)sRegistered=false;
 if(scenario==3)gpMarDirector=nullptr;
 if(scenario==4)sPendingDirector=&otherDirector;
 if(scenario==5)director._260=0;
 if(scenario==6)sLoadedGeneration=1;
 if(scenario==7)sView=nullptr;
 if(scenario==8)sViewWord=nullptr;
 if(scenario==9)sViewWord=(void**)0x80000001;
 if(scenario==10)sViewWord=(void**)0x817ffffd;
 if(scenario==11)*word=(void*)0x5678;
 if(scenario==12)failAt=0;
 return preserveSavestateBindings(keepWord)||keptCount?4:0;
}
API int ghostRetire(unsigned scenario){
 a.count=b.count=0;player={&a,&b};gpMarioOriginal=&player;
 sSubmitted[0]=sSubmitted[1]=sPrepared[0]=sPrepared[1]=true;
 sPreparedAttachments[0]=sPreparedAttachments[1]=(void*)1;
 if(scenario==1)gpMarioOriginal=nullptr;
 if(scenario==2)player.mDrawBufferB=nullptr;
 onSavestateLoaded();
 if(sSubmitted[0]||sSubmitted[1]||sPrepared[0]||sPrepared[1]||sPreparedAttachments[0]||sPreparedAttachments[1])return 1;
 return a.count==(scenario?0:1)&&b.count==(scenario?0:1)?0:2;
}
}
'''
        cpp, dll = work / "bindings.cpp", work / "bindings.dll"
        cpp.write_text(code)
        result = subprocess.run([
            str(compiler), "--target=x86_64-pc-windows-msvc", "-shared",
            "-nostdlib", "-fno-builtin", "-fuse-ld=lld", "-Wl,/noentry",
            str(cpp), "-o", str(dll),
        ], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        cls.lib = C.CDLL(str(dll))
        cls.addClassCleanup(lambda: C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))
        C.windll.kernel32.VirtualAlloc.restype = C.c_void_p
        address = C.windll.kernel32.VirtualAlloc(C.c_void_p(0x81000000), 4096, 0x3000, 4)
        if address != 0x81000000:
            raise AssertionError("Test MEM1 mapping unavailable")
        cls.addClassCleanup(lambda: C.windll.kernel32.VirtualFree(C.c_void_p(address), 0, 0x8000))
        cls.lib.ghostRun.argtypes = [C.c_uint, C.c_void_p]
        cls.word = address

    def test_mario_preserves_only_current_callback_words(self):
        self.assertEqual(self.lib.MarioColorsRun(0), 0)

    def test_fludd_preserves_only_current_callback_words(self):
        self.assertEqual(self.lib.FluddColorsRun(0), 0)

    def test_packet_registries_reject_unready_stale_or_changed_bindings(self):
        for feature in (self.lib.MarioColorsRun, self.lib.FluddColorsRun):
            for scenario in range(1, 12):
                with self.subTest(feature=feature.__name__, scenario=scenario):
                    self.assertEqual(feature(scenario), 0)

    def test_ghost_preserves_only_registered_live_node_word(self):
        self.assertEqual(self.lib.ghostRun(0, self.word), 0)

    def test_ghost_rejects_changed_owner_generation_pointer_or_callback_failure(self):
        for scenario in range(1, 13):
            with self.subTest(scenario=scenario):
                self.assertEqual(self.lib.ghostRun(scenario, self.word), 0)

    def test_restore_retires_draw_buffers_and_prepared_packets(self):
        for scenario in range(3):
            with self.subTest(scenario=scenario):
                self.assertEqual(self.lib.ghostRetire(scenario), 0)


if __name__ == "__main__":
    unittest.main()
