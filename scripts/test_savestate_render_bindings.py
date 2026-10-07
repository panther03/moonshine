"""Run typed render-binding preservation against guarded live fixtures."""

import ctypes as C
import re
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
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned u32;
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
        shared = (ROOT / "include/susamune/model_color_draw.hxx").read_text()
        code += "namespace ModelColorDraw {" + function(shared, "live") + "}\n"
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
        mario_header = (ROOT / "include/SMS/Player/Mario.hxx").read_text()
        # Derive widths from the production header, rather than a permissive
        # host-only u32 stub which masks PPC big-endian halfword mistakes.
        visibility_fields = "\n".join(re.search(
            rf"^\s*(u\d+ {field};)", mario_header, re.MULTILINE
        ).group(1) for field in ("_114", "_116"))
        code += r'''
namespace GhostModel {
struct J3DDrawBuffer {unsigned count,queued;void frameInit(){++count;queued=0;}}a,b,otherA,otherB;
alignas(8) u8 j3dSys[0x60];
const u32 kCueEntry=0x200;
unsigned entries,fluddEntries,badEntry,sequence;
u32 expectedStatus;
struct TWaterGun {
 void perform(u32 cue,void*graphics){
  auto buffers=reinterpret_cast<J3DDrawBuffer**>(j3dSys+0x44);
  if(cue!=kCueEntry||graphics||buffers[0]!=&a||buffers[1]!=&b||sequence++)++badEntry;
  ++fluddEntries;b.queued|=2;
 }
}fludd;
struct TMario {
 J3DDrawBuffer*mDrawBufferA,*mDrawBufferB;
 VISIBILITY_FIELDS
 u32 mAttributes,mPerformFlags,mState;
 TWaterGun*mFludd;
 void entryModels(void*graphics){
  auto buffers=reinterpret_cast<J3DDrawBuffer**>(j3dSys+0x44);
  if(graphics||buffers[0]!=&a||buffers[1]!=&b||sequence!=fluddEntries||mState!=(expectedStatus&~0x10000u))++badEntry;
  ++entries;a.queued|=1;
 }
}player,*gpMarioOriginal=&player;
static_assert(sizeof(player._114)==2&&sizeof(player._116)==2,"retail flags are two halfwords");
static_assert(__builtin_offsetof(TMario,_116)==__builtin_offsetof(TMario,_114)+2,"retail flag offsets");
static_assert(__builtin_offsetof(TMario,mAttributes)==__builtin_offsetof(TMario,_114)+4,"attributes follow at +0x118");
bool sRegistered,sSubmitted[2],sPrepared[2];void*sPreparedAttachments[2];
TMarDirector*sPendingDirector;u32 sLoadedGeneration,sPendingGeneration;
void*sView;void**sViewWord;
'''
        code = code.replace("VISIBILITY_FIELDS", visibility_fields)
        # Keep the host address full-width; the real PPC pointer is 32 bits.
        preserve = function(ghost, "preserveSavestateBindings").replace(
            "const u32 address = reinterpret_cast<u32>(sViewWord);",
            "const auto address = reinterpret_cast<__UINTPTR_TYPE__>(sViewWord);",
        )
        code += preserve + "\n".join(function(ghost, name) for name in (
            "retirePlayerDrawBuffers", "clearPreparedPackets",
            "rebuildPlayerDrawBuffers", "onSavestateLoaded"))
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
 a={0,0x80};b={0,0x80};otherA={9,0x1234};otherB={8,0x5678};
 // Observed retail bytes at Mario+0x114: 04 12 00 00; +0x118: 00 00 80 00.
 // Decode only host byte order. Widths/layout above come from the real header.
 const u8 nativeBytes[]={0x04,0x12,0x00,0x00,0x00,0x00,0x80,0x00};
 player={&a,&b,u16((nativeBytes[0]<<8)|nativeBytes[1]),
  u16((nativeBytes[2]<<8)|nativeBytes[3]),
  u32((nativeBytes[4]<<24)|(nativeBytes[5]<<16)|(nativeBytes[6]<<8)|nativeBytes[7]),
  0,0x80001,&fludd};gpMarioOriginal=&player;
 entries=fluddEntries=badEntry=sequence=0;
 auto buffers=reinterpret_cast<J3DDrawBuffer**>(j3dSys+0x44);
 buffers[0]=&otherA;buffers[1]=&otherB;
 sSubmitted[0]=sSubmitted[1]=sPrepared[0]=sPrepared[1]=true;
 sPreparedAttachments[0]=sPreparedAttachments[1]=(void*)1;
 if(scenario==1)gpMarioOriginal=nullptr;
 if(scenario==2)player.mDrawBufferB=nullptr;
 if(scenario==3)player._114=0;
 if(scenario==4)player.mAttributes|=4;
 if(scenario==5)player.mAttributes|=0x200000;
 if(scenario==6)player.mPerformFlags=kCueEntry;
 if(scenario==7)player.mAttributes=0;
 if(scenario==8)player.mFludd=nullptr;
 if(scenario==9){player._114|=1;player.mAttributes|=0x10;player.mPerformFlags=1;}
 if(scenario==10)player.mState=0x810446; // ridden Blooper
 if(scenario==11)player.mState=0x800447; // cart: keep exact status
 if(scenario==12)player._116=0xffff; // adjacent halfword is not visibility
 if(scenario==13){player._114=0x410;player._116=2;} // low word cannot make Mario visible
 expectedStatus=player.mState;
 onSavestateLoaded();
 if(sSubmitted[0]||sSubmitted[1]||sPrepared[0]||sPrepared[1]||sPreparedAttachments[0]||sPreparedAttachments[1])return 1;
 const bool retired=scenario!=1&&scenario!=2;
 const bool visible=scenario==0||(scenario>=7&&scenario!=13);
 const bool wet=visible&&scenario!=7&&scenario!=8;
 if(a.count!=retired||b.count!=retired)return 2;
 if(entries!=visible||fluddEntries!=wet||badEntry)return 3;
 if(a.queued!=(retired?unsigned(visible):0x80)||b.queued!=(retired?unsigned(wet)*2:0x80))return 4;
 if(buffers[0]!=&otherA||buffers[1]!=&otherB||otherA.count!=9||otherB.count!=8||otherA.queued!=0x1234||otherB.queued!=0x5678)return 5;
 if(player.mState!=expectedStatus)return 6;
 return 0;
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

    def test_restore_rebuilds_visible_mario_without_simulation_or_other_queue_changes(self):
        for scenario in range(14):
            with self.subTest(scenario=scenario):
                self.assertEqual(self.lib.ghostRetire(scenario), 0)


if __name__ == "__main__":
    unittest.main()
