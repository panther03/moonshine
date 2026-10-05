"""Exercise the production Mario colour editor and its Appearance menu route."""

import ctypes
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_native_timer_creation import function
from test_nested_menu_focus import function as method

ROOT = Path(__file__).resolve().parents[1]


class MarioColorTests(unittest.TestCase):
    GROUP = "Mario"
    COUNT = 7

    @classmethod
    def setUpClass(cls):
        compiler = ROOT / "toolchain/clang++.exe"
        if not compiler.exists():
            raise unittest.SkipTest("Bundled Windows compiler required")
        cls.temp = tempfile.TemporaryDirectory(prefix="moonshine-mario-editor-")
        cls.addClassCleanup(cls.temp.cleanup)
        work = Path(cls.temp.name)
        (work / "Dolphin").mkdir()
        (work / "Dolphin/types.h").write_text(
            "typedef unsigned char u8; typedef signed char s8; typedef unsigned short u16;"
            "typedef short s16; typedef unsigned u32; typedef int s32;", encoding="ascii")
        creation = (ROOT / "src/creation.cpp").read_text()
        layout = (ROOT / "src/layout_editor.cpp").read_text()
        mario = (ROOT / "src/mario_colors.cpp").read_text()
        fludd = (ROOT / "src/fludd_colors.cpp").read_text()
        raw = (ROOT / "include/susamune/raw_prompt_input.hxx").read_text()
        raw = raw[raw.index("class RawPromptInput"):raw.index("#endif")]
        menu = (ROOT / "src/menu.cpp").read_text()
        category = menu[menu.index("class CategorySettingsTab :"):]
        prelude = r'''
#include "susamune/susamune_cfg.h"
#include "susamune/creation.hxx"
#include "susamune/creation_color.hxx"
#include "susamune/mario_colors.hxx"
#include "susamune/fludd_colors.hxx"
#define API extern "C" __declspec(dllexport)
extern "C" void *memset(void *p,int v,unsigned long long n) {u8*b=(u8*)p;while(n--)*b++=(u8)v;return p;}
extern "C" void *memcpy(void *d,const void*s,unsigned long long n) {u8*a=(u8*)d;const u8*b=(const u8*)s;while(n--)*a++=*b++;return d;}
struct JUTGamePad {
 enum {A=1,B=2,X=4,Y=8,Z=16,START=32,L=64,R=128,DPAD_LEFT=256,DPAD_RIGHT=512,
 DPAD_UP=1024,DPAD_DOWN=2048,CSTICK_LEFT=4096,CSTICK_RIGHT=8192,CSTICK_UP=16384,CSTICK_DOWN=32768};
 struct Status {u16 mButton;};static Status mPadStatus[1];
};
JUTGamePad::Status JUTGamePad::mPadStatus[1];
class TMarioGamePad:public JUTGamePad {public:struct{u32 mInput,mFrameInput,mRapidInput;}mButtons;};
class Menu {public:u32 navigationInput(TMarioGamePad*p){return p->mButtons.mRapidInput;}
 void toast(const char*){}void factoryReset(){} };
void CreationEditor::draw(Menu*,const char*,const char*)const{}
'''
        shared = (ROOT / "include/susamune/model_color_editor.hxx").read_text()
        shared = re.sub(r"^#(?:include|ifndef|define|endif)[^\n]*", "", shared, flags=re.M)
        code = prelude + raw + shared + re.sub(r"^#include[^\n]*", "", (ROOT / "src/creation_color.cpp").read_text(), flags=re.M)
        code += creation[creation.index("enum EditOption"):creation.index("inline int clampi")]
        code += function(creation, "clampi")
        code += "namespace LayoutEditor {" + function(layout, "updatePositionScale") + "}\n"
        code += "\n".join(function(creation, n) for n in (
            "adjustTextChannel", "resetOption", "CreationEditor::reset",
            "CreationEditor::begin", "CreationEditor::optionEnabled", "CreationEditor::moveOption",
            "CreationEditor::repeatInput", "CreationEditor::update"))
        code += "namespace Creation {" + function(creation, "fillWhite") + "}\n"
        code += re.sub(r"^#(?:include|pragma)[^\n]*", "", mario, flags=re.M)
        code += re.sub(r"^#(?:include|pragma)[^\n]*", "", fludd, flags=re.M)
        fixture_start = len(code)
        code += r'''
TMarioGamePad pad={};Menu menu;
void sample(u16 raw=0,u32 held=0,u32 rapid=0) {
 JUTGamePad::mPadStatus[0].mButton=raw;
 pad.mButtons.mInput=held;pad.mButtons.mFrameInput=held;pad.mButtons.mRapidInput=rapid;
 MarioColors::updateEditor(&pad);
}
void click(u16 button) {sample();sample(button);sample();}
void nav(u32 direction) {sample();sample(0,direction);sample();}
void lightness(){for(int i=0;i<3;i++)nav(JUTGamePad::CSTICK_DOWN);}
void appearance(){for(int i=0;i<3;i++)nav(JUTGamePad::CSTICK_UP);}
void finish(bool keep) {click(keep?JUTGamePad::A:JUTGamePad::B);click(JUTGamePad::A);}
void initialize() {JUTGamePad::mPadStatus[0].mButton=0;pad={};MarioColors::resetDefaults();}
unsigned mask(){unsigned result=0;for(unsigned i=0;i<7;i++)if(MarioColors::enabled(i))result|=1u<<i;return result;}
API int editor(int scenario) {
 initialize();
 if(MarioColors::rgb(7)||MarioColors::enabled(7)||MarioColors::editing()||MarioColors::dirty()||mask())return 1;
 for(unsigned i=0;i<7;i++)for(unsigned c=0;c<3;c++)if(MarioColors::rgb(i)[c]!=255)return 2;
 if(scenario==0){
  SusamuneMarioColorsCfg cfg={};cfg.magic=SUSAMUNE_MARIO_COLORS_MAGIC;cfg.version=1;cfg.enabled=0x55;
  for(unsigned i=0;i<7;i++)for(unsigned c=0;c<3;c++)cfg.rgb[i][c]=i*3+c;
  MarioColors::adopt(&cfg);if(mask()!=0x55||MarioColors::dirty())return 3;
  cfg.enabled=0x80;MarioColors::adopt(&cfg);if(mask()!=0x55)return 4;
  cfg.enabled=0;cfg.version=2;MarioColors::adopt(&cfg);if(mask()!=0x55)return 5;
  cfg.version=1;cfg.magic=0;MarioColors::adopt(&cfg);MarioColors::adopt(nullptr);if(mask()!=0x55)return 6;
  memset(&cfg,0xa5,sizeof(cfg));MarioColors::stageInto(&cfg);
  if(cfg.magic!=SUSAMUNE_MARIO_COLORS_MAGIC||cfg.version!=1||cfg.enabled!=0x55||cfg.reserved0)return 7;
  for(unsigned i=0;i<3;i++)if(cfg.reserved[i])return 8;
  for(unsigned i=0;i<7;i++)for(unsigned c=0;c<3;c++)if(cfg.rgb[i][c]!=i*3+c)return 9;
  MarioColors::resetDefaults();MarioColors::adopt(&cfg);
  return mask()==0x55&&MarioColors::rgb(6)[2]==20?0:10;
 }
 MarioColors::beginEditor();
 if(scenario==1){
  nav(JUTGamePad::CSTICK_RIGHT);if(mask()!=0x7f)return 11;
  click(JUTGamePad::START);nav(JUTGamePad::CSTICK_LEFT);if(mask()!=0x7e)return 12;
  lightness();nav(JUTGamePad::CSTICK_LEFT);
  if(mask()!=0x7f||MarioColors::rgb(0)[0]!=245||MarioColors::rgb(1)[0]!=255)return 13;
  sample(0,JUTGamePad::Y|JUTGamePad::CSTICK_LEFT);sample();
  if(MarioColors::rgb(0)[0]!=242)return 14;
  appearance();nav(JUTGamePad::CSTICK_LEFT);
  if(mask()!=0x7e||MarioColors::rgb(0)[0]!=242)return 15;
  nav(JUTGamePad::CSTICK_RIGHT);click(JUTGamePad::Z);click(JUTGamePad::A);
  if(mask()!=0x7e||MarioColors::rgb(0)[0]!=242)return 16;
  lightness();click(JUTGamePad::Z);click(JUTGamePad::A);
  if(mask()!=0x7f||MarioColors::rgb(0)[0]!=255)return 17;
  finish(true);return !MarioColors::editing()&&MarioColors::dirty()?0:18;
 }
 if(scenario==2){
  lightness();nav(JUTGamePad::CSTICK_LEFT);
  if(mask()!=0x7f||MarioColors::dirty())return 19;
  for(unsigned i=0;i<7;i++)if(MarioColors::rgb(i)[0]!=245)return 20;
  finish(false);if(mask()||MarioColors::dirty()||MarioColors::editing())return 21;
  for(unsigned i=0;i<7;i++)if(MarioColors::rgb(i)[0]!=255)return 22;
  MarioColors::beginEditor();nav(JUTGamePad::CSTICK_RIGHT);finish(true);
  if(!MarioColors::dirty()||mask()!=0x7f)return 23;
  MarioColors::beginEditor();nav(JUTGamePad::CSTICK_LEFT);finish(false);
  if(!MarioColors::dirty()||mask()!=0x7f)return 24;
  MarioColors::clearDirty();return MarioColors::dirty()?25:0;
 }
 if(scenario==3){
  for(unsigned i=1;i<=7;i++){
   click(JUTGamePad::START);lightness();nav(JUTGamePad::CSTICK_LEFT);
   if(mask()!=((1u<<i)-1))return 26;
   for(unsigned j=0;j<7;j++)if(MarioColors::rgb(j)[0]!=(j<i?245:255))return 27;
   appearance();
  }
  click(JUTGamePad::START);nav(JUTGamePad::CSTICK_LEFT);if(mask())return 28;
  finish(true);SusamuneMarioColorsCfg cfg={};MarioColors::stageInto(&cfg);MarioColors::resetDefaults();MarioColors::adopt(&cfg);
  if(mask())return 29;
  for(unsigned i=0;i<7;i++)if(MarioColors::rgb(i)[0]!=245)return 30;
  MarioColors::beginEditor();nav(JUTGamePad::CSTICK_RIGHT);finish(true);return mask()==0x7f?0:31;
 }
 return 99;
}
API int heldConfirmation(){
 initialize();JUTGamePad::mPadStatus[0].mButton=JUTGamePad::A;MarioColors::beginEditor();
 for(int i=0;i<30;i++)sample(JUTGamePad::A,0,JUTGamePad::A);
 if(!MarioColors::editing())return 1;
 sample();sample(0,JUTGamePad::CSTICK_RIGHT,JUTGamePad::A);
 if(mask()!=0x7f)return 2;
 sample(JUTGamePad::A);for(int i=0;i<30;i++)sample(JUTGamePad::A,0,JUTGamePad::A);
 if(!MarioColors::editing())return 3;
 sample();sample(JUTGamePad::A);
 if(!MarioColors::editing())return 4;
 for(int i=0;i<30;i++)sample(JUTGamePad::A,0,JUTGamePad::A);
 if(!MarioColors::editing())return 5;
 sample();return !MarioColors::editing()&&MarioColors::dirty()?0:6;
}
API int saveDuringPreview(int keep){
 initialize();MarioColors::beginEditor();lightness();nav(JUTGamePad::CSTICK_LEFT);finish(true);
 if(!MarioColors::dirty()||mask()!=0x7f)return 1;
 MarioColors::beginEditor();nav(JUTGamePad::CSTICK_LEFT);
 if(!MarioColors::dirty()||mask())return 2;
 lightness();nav(JUTGamePad::CSTICK_LEFT);
 if(MarioColors::rgb(0)[0]!=235)return 3;
 SusamuneMarioColorsCfg cfg={};MarioColors::stageInto(&cfg);MarioColors::clearDirty();
 if(cfg.enabled!=0x7f||cfg.rgb[0][0]!=245||MarioColors::dirty())return 4;
 finish(keep!=0);
 if(MarioColors::dirty()!=(keep!=0))return 5;
 if(mask()!=0x7f||MarioColors::rgb(0)[0]!=(keep?235:245))return 6;
 MarioColors::stageInto(&cfg);
 if(cfg.rgb[0][0]!=(keep?235:245))return 7;
 return 0;
}
'''
        if cls.GROUP == "Fludd":
            fixture = code[fixture_start:].replace("MarioColors", "FluddColors").replace("MARIO_COLORS", "FLUDD_COLORS")
            fixture = fixture.replace("i<7", "i<10").replace("j<7", "j<10").replace("i<=7", "i<=10")
            fixture = fixture.replace("::rgb(7)", "::rgb(10)").replace("::enabled(7)", "::enabled(10)")
            fixture = fixture.replace("0x7f", "0x3ff").replace("0x7e", "0x3fe").replace("cfg.enabled=0x80", "cfg.enabled=0x400")
            fixture = fixture.replace("||cfg.reserved0", "").replace("i<3;i++)if(cfg.reserved", "i<26;i++)if(cfg.reserved")
            code = code[:fixture_start] + fixture
        # Other menu features are inert; all Mario gates, selection, modal routing
        # and input suppression below are the production methods.
        code += r'''
typedef int SettingId;
enum{SETTING_COUNT=256};
struct Settings{static bool favoriteable(int){return false;}void toggleFavorite(int){}bool favorite(int){return false;}void cycle(int,int){}}gSettings;
namespace RecordsPersistence{void resetAll(){}}
struct Extras{bool editing(){return false;}void updateEditor(TMarioGamePad*){}
 void beginSavestateFeedbackEditor(){}void beginNativeTimerEditor(){}
 void beginWallkickEditor(){}void beginRolloutEditor(){}void beginDustEditor(){}
 void beginPracticeDisplayEditor(unsigned){}}gCreationExtras;
struct Qft{bool editing(){return false;}void updateEditor(TMarioGamePad*){}void beginEditor(){}}gQftDisplay;
int wrap(int value,int n){return (value+n)%n;}
const u8 kAppearanceMarioSettings[]={1,2,3};const u8 otherSettings[]={4};
class CategorySettingsTab{public:
 int mSel=0,mMode=1;bool appearance=true;struct Page{const u8*ids;}page={kAppearanceMarioSettings};
 bool isAppearance()const{return appearance;}const Page&currentPage()const{return page;}
 bool hasFactoryReset()const{return false;}bool hasTimerEditors()const{return false;}bool hasFeedbackEditor()const{return false;}
 bool hasMovementEditors()const{return false;}bool hasNativeTimerEditor()const{return false;}
 bool resetConfirm()const{return false;}bool pageRoot()const{return !mMode;}
 bool hasPages()const{return true;}bool isStarred()const{return false;}
 int buildList(u8*ids)const{for(int i=0;i<3;i++)ids[i]=i;return 3;}
 void jumpSection(const u8*,int,int,int){}void updatePageRoot(Menu*,TMarioGamePad*){}
'''
        code += "\n".join(method(category, n).replace(" override", "") for n in (
            "bool hasMarioColorsEditor()", "bool hasVisualEditor()", "int extraRows()",
            "bool grabsInput()", "bool suppressesBinds()", "void update(Menu *menu")) + "};\n"
        code += r'''
API int menuRoute(){
 initialize();CategorySettingsTab tab;
 if(tab.extraRows()!=2||tab.grabsInput())return 1;
 tab.mMode=0;if(tab.extraRows())return 2;tab.mMode=1;
 tab.page.ids=otherSettings;if(tab.extraRows())return 3;tab.page.ids=kAppearanceMarioSettings;
 tab.appearance=false;if(tab.extraRows())return 4;tab.appearance=true;
 tab.mSel=200;pad.mButtons.mRapidInput=0;tab.update(&menu,&pad);if(tab.mSel!=4)return 5;tab.mSel=3;
 JUTGamePad::mPadStatus[0].mButton=JUTGamePad::A;pad.mButtons.mRapidInput=JUTGamePad::A;
 tab.update(&menu,&pad);if(!tab.grabsInput()||!tab.suppressesBinds())return 6;
 for(int i=0;i<30;i++)tab.update(&menu,&pad);
 if(!MarioColors::editing()||tab.mSel!=3)return 7;
 sample();nav(JUTGamePad::CSTICK_RIGHT);finish(true);
 if(tab.grabsInput()||!MarioColors::dirty())return 8;
 pad={};pad.mButtons.mRapidInput=JUTGamePad::CSTICK_DOWN;tab.update(&menu,&pad);
 return tab.mSel==4?0:9;
}
'''
        if cls.GROUP == "Fludd":
            start = code.index("API int menuRoute()")
            route = code[start:].replace("MarioColors", "FluddColors")
            route = route.replace("tab.mSel=3;", "tab.mSel=4;").replace("tab.mSel!=3", "tab.mSel!=4")
            route = route.replace("tab.mSel==4?0:9", "tab.mSel==0?0:9")
            code = code[:start] + route
        source = work / "mario.cpp"
        source.write_text(code, encoding="ascii")
        dll = work / "mario.dll"
        subprocess.run([str(compiler), "--target=x86_64-pc-windows-msvc", "-shared",
                        "-nostdlib", "-fuse-ld=lld", "-Wl,/noentry", "-O2",
                        "-I", str(work), "-I", str(ROOT / "include"),
                        str(source), "-o", str(dll)], check=True)
        cls.lib = ctypes.CDLL(str(dll))
        from _ctypes import FreeLibrary
        cls.addClassCleanup(FreeLibrary, cls.lib._handle)

    def test_defaults_configuration_validation_and_all_seven_parts_roundtrip(self):
        self.assertEqual(self.lib.editor(0), 0)

    def test_original_custom_hsl_fine_adjustment_and_option_reset(self):
        self.assertEqual(self.lib.editor(1), 0)

    def test_keep_discard_restore_colors_modes_and_previous_dirty_status(self):
        self.assertEqual(self.lib.editor(2), 0)

    def test_each_part_and_all_original_retain_custom_rgb(self):
        self.assertEqual(self.lib.editor(3), 0)

    def test_raw_release_required_for_entry_keep_confirmation_and_exit(self):
        self.assertEqual(self.lib.heldConfirmation(), 0)

    def test_save_during_preview_uses_confirmed_copy_and_keep_discard_stay_dirty_correctly(self):
        for keep in (0, 1):
            self.assertEqual(self.lib.saveDuringPreview(keep), 0)

    def test_mario_page_route_clamps_selection_and_suppresses_binds_in_modal(self):
        self.assertEqual(self.lib.menuRoute(), 0)


if __name__ == "__main__":
    unittest.main()
