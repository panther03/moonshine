"""Exercise independent feedback styles through the real shared Creation editor."""
import ctypes as C
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_native_timer_creation import function

ROOT = Path(__file__).resolve().parents[1]


class PracticeDisplayEditorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / "toolchain/clang++.exe"
        if not compiler.exists():
            raise unittest.SkipTest("Bundled Windows compiler required")
        cls.temp = tempfile.TemporaryDirectory(prefix="moonshine-feedback-editors-")
        cls.addClassCleanup(cls.temp.cleanup)
        work = Path(cls.temp.name)
        (work / "Dolphin").mkdir()
        (work / "Dolphin/types.h").write_text(
            "typedef unsigned char u8;typedef unsigned short u16;typedef unsigned u32;")
        creation = (ROOT / "src/creation.cpp").read_text()
        extras = (ROOT / "src/creation_extras.cpp").read_text()
        code = r'''
#define private public
#include "susamune/creation.hxx"
#include "susamune/creation_color.hxx"
#include "susamune/creation_extras.hxx"
#undef private
#include "susamune/settings_list.h"
#define SETTING_ENUM(name,key) name,
enum SettingId { SUSAMUNE_SETTING_LIST(SETTING_ENUM) SETTING_COUNT };
#undef SETTING_ENUM
struct Settings {
 static const char *name(SettingId){return "Setting";}
 int get(SettingId id)const{return id==SETTING_STREAK_FAILURE_SIZE?5:
  id==SETTING_STREAK_FAILURE_X?7:id==SETTING_STREAK_FAILURE_Y?17:0;}
} gSettings;
#define API extern "C" __declspec(dllexport)
extern "C" void *memset(void*p,int v,unsigned long long n){u8*b=(u8*)p;while(n--)*b++=(u8)v;return p;}
extern "C" void *memcpy(void*d,const void*s,unsigned long long n){u8*a=(u8*)d;const u8*b=(const u8*)s;while(n--)*a++=*b++;return d;}
extern "C" int memcmp(const void*a,const void*b,unsigned long long n){for(unsigned i=0;i<n;++i)if(((const u8*)a)[i]!=((const u8*)b)[i])return ((const u8*)a)[i]-((const u8*)b)[i];return 0;}
extern "C" unsigned long long strlen(const char*s){unsigned n=0;while(s[n])++n;return n;}
struct TMarioGamePad {
 enum{A=1,B=2,X=4,Y=8,Z=16,START=32,L=64,R=128,DPAD_LEFT=256,DPAD_RIGHT=512,
 DPAD_UP=1024,DPAD_DOWN=2048,CSTICK_LEFT=4096,CSTICK_RIGHT=8192,CSTICK_UP=16384,CSTICK_DOWN=32768};
 struct{u32 mInput,mFrameInput,mRapidInput;}mButtons;
};
namespace PackedText {const char*at(const char*p,int n){while(n--){while(*p)++p;++p;}return p;}}
const char kNativeTimerNames[]="";
const u8 kHealthDefaults[2][3]={{255,255,255},{0,255,255}};
'''
        code += re.sub(r"^#include[^\n]*", "", (ROOT / "src/creation_color.cpp").read_text(), flags=re.M)
        code += creation[creation.index("enum EditOption"):creation.index("inline int clampi")]
        code += function(creation, "clampi")
        code += "namespace LayoutEditor {" + function(
            (ROOT / "src/layout_editor.cpp").read_text(), "updatePositionScale") + "}\n"
        code += "\n".join(function(creation, name) for name in (
            "adjustTextChannel", "resetOption", "CreationEditor::reset", "CreationEditor::begin",
            "CreationEditor::optionEnabled", "CreationEditor::moveOption",
            "CreationEditor::repeatInput", "CreationEditor::update"))
        code += extras[extras.index("const char kWallkickNames"):extras.index("inline int clampi")]
        code += "\n".join(function(extras, name) for name in (
            "practiceDisplayName", "nativeTimerColorSlot", "copyRgb", "clampStyle", "storeStyle",
            "CreationExtras::defaultWordStyle", "defaultRecentIlStyle", "defaultSavestateFeedbackStyle",
            "CreationExtras::defaultWallkickStyle", "CreationExtras::defaultPracticeStyle",
            "defaultAchievementBannerStyle", "defaultToastStyle",
            "defaultPbBannerStyle", "defaultStageSessionStyle", "defaultNativeTimerStyle",
            "CreationExtras::beginOverlayEditor", "CreationExtras::beginWallkickEditor",
            "CreationExtras::beginRolloutEditor", "CreationExtras::beginDustEditor",
            "CreationExtras::beginPracticeDisplayEditor", "CreationExtras::beginNativeTimerEditor",
            "CreationExtras::beginFailureBannerEditor",
            "CreationExtras::beginColorEditor", "CreationExtras::beginWordEditor",
            "CreationExtras::adoptPracticeDisplays", "CreationExtras::stagePracticeDisplaysInto",
            "CreationExtras::updateEditor"))
        code += r'''
void CreationExtras::applyHud(){}
void CreationExtras::beginHudPreview(int){}
void CreationExtras::endHudPreview(){}
void CreationExtras::clampWord(int){}
void CreationExtras::updateKeyboard(TMarioGamePad*){}
struct Menu{};
CreationStyle drawnStyle;u8 drawnRgb[3];unsigned draws;
namespace Creation {void drawTextBox(Menu*menu,const CreationStyle&s,const u8(*rgb)[3],u16,const char*text,bool,u16){
 if(!menu||!text)return;
 drawnStyle=s;memcpy(drawnRgb,rgb,3);++draws;}}
'''
        code += "\n".join(function(extras, name) for name in (
            "drawMovementFeedback", "CreationExtras::drawPracticeDisplay",
            "CreationExtras::drawWallkickDisplay", "CreationExtras::drawRolloutDisplay",
            "CreationExtras::drawDustDisplay"))
        code += r'''
CreationExtras state;
static u32 visualCopy(u8*out){
 u32 n=0;
#define COPY(v) memcpy(out+n,&state.v,sizeof(state.v));n+=sizeof(state.v)
 COPY(mWallkickStyle);COPY(mWallkickRgb);COPY(mRolloutStyle);COPY(mRolloutRgb);
 COPY(mDustStyle);COPY(mDustRgb);COPY(mPracticeDisplays);COPY(mNativeTimerStyle);
 COPY(mColors);COPY(mDefaultColors);COPY(mWordStyle);COPY(mWordRgb);COPY(mWords);COPY(mFailureBanner);
#undef COPY
 return n;
}
API void reset(){
 memset(&state,0,sizeof(state));state.mEditor.reset();
 state.mWallkickStyle=state.mRolloutStyle=state.mDustStyle=CreationExtras::defaultWallkickStyle();
 state.mNativeTimerStyle=defaultNativeTimerStyle();
 SusamunePracticeDisplayStyleCfg cfg;SusamunePracticeDisplayStyleInit(&cfg);
 state.adoptPracticeDisplays(&cfg);
 for(unsigned i=0;i<SUSAMUNE_CREATION_COLOR_COUNT;++i)for(unsigned c=0;c<3;++c){
  state.mColors[i][c]=20+i+c;state.mDefaultColors[i][c]=80+i+c;}
 for(unsigned i=0;i<7;++i)for(unsigned c=0;c<3;++c){
  state.mWallkickRgb[i][c]=30+i+c;state.mDustRgb[i][c]=60+i+c;
  if(i<5)state.mRolloutRgb[i][c]=90+i+c;
  for(unsigned d=0;d<3;++d)state.mPracticeDisplays[d].rgb[i][c]=120+d*20+i+c;
 }
 for(unsigned w=0;w<3;++w){state.mWordLength[w]=32;state.mWordStyle[w]=CreationExtras::defaultWordStyle(w);
  for(unsigned i=0;i<32;++i)for(unsigned c=0;c<3;++c)state.mWordRgb[w][i][c]=40+w+i+c;}
 draws=0;
}
API u32 snapshot(u8*out){return visualCopy(out);}
API void begin(unsigned target){
 if(target==0)state.beginWallkickEditor();else if(target==1)state.beginRolloutEditor();
 else if(target==2)state.beginDustEditor();else if(target<6)state.beginPracticeDisplayEditor(target-3);
 else if(target==6)state.beginNativeTimerEditor();
 else if(target==7)state.beginColorEditor(0,SUSAMUNE_CREATION_COLOR_COUNT,"All HUD");
 else if(target==8)state.beginWordEditor(0);else state.beginFailureBannerEditor();
}
API u32 editing(){return state.editing();}
API u32 slots(){return state.mEditor.mTextSlots;}
API u32 dirty(){return state.mDirty;}
API void select(unsigned target){state.mEditor.selectTarget(target);}
API void edit(unsigned option){
 TMarioGamePad pad={};
 if(option==0)pad.mButtons.mRapidInput=TMarioGamePad::DPAD_RIGHT;
 else if(option==1)pad.mButtons.mInput=pad.mButtons.mFrameInput=TMarioGamePad::R;
 else{state.mEditor.mOption=OPTION_TEXT_L;pad.mButtons.mInput=pad.mButtons.mFrameInput=TMarioGamePad::CSTICK_LEFT;}
 state.updateEditor(&pad);
}
API void finish(unsigned keep){
 TMarioGamePad pad={};pad.mButtons.mRapidInput=keep?TMarioGamePad::A:TMarioGamePad::B;
 state.updateEditor(&pad);pad.mButtons.mRapidInput=TMarioGamePad::A;state.updateEditor(&pad);
}
API unsigned scratchCheck(){
 if(state.mEditor.mTextRgb!=state.mWordBackup||state.mEditor.mBackupRgb!=state.mWordBackup+30)return 1;
 for(unsigned i=0;i<15;++i)for(unsigned c=0;c<3;++c){
  if(state.mWordBackup[15+i][c]!=80+nativeTimerColorSlot(i)+c)return 2;
  if(state.mWordBackup[30+i][c]!=20+nativeTimerColorSlot(i)+c)return 3;
 }
 return 0;
}
API void stage(SusamunePracticeDisplayStyleCfg*out){state.stagePracticeDisplaysInto(out);}
API void adopt(const SusamunePracticeDisplayStyleCfg*in){state.adoptPracticeDisplays(in);}
API void draw(unsigned display,unsigned color){Menu menu;state.drawPracticeDisplay(&menu,"Long timing preview",display,color);}
API void drawOld(unsigned display,int color,unsigned missing){
 Menu menu;Menu*p=missing==1?nullptr:&menu;const char*t=missing==2?nullptr:"Old timing preview";
 if(display==0)state.drawWallkickDisplay(p,t,color);
 else if(display==1)state.drawRolloutDisplay(p,t,color);
 else state.drawDustDisplay(p,t,color);
}
API unsigned drawn(unsigned field){if(field==0)return draws;if(field==1)return drawnStyle.x;
 if(field==2)return drawnStyle.y;if(field==3)return drawnStyle.scale;return drawnRgb[field-4];}
'''
        path = work / "fixture.cpp"
        path.write_text(code)
        library = path.with_suffix(".dll")
        result = subprocess.run([str(compiler), "--target=x86_64-pc-windows-msvc", "-shared",
            "-nostdlib", "-fno-builtin", "-fuse-ld=lld", "-Wl,/noentry", "-O2",
            "-I", str(work), "-I", str(ROOT / "include"), str(path), "-o", str(library)],
            capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)
        cls.lib = C.CDLL(str(library))
        from _ctypes import FreeLibrary
        cls.addClassCleanup(FreeLibrary, cls.lib._handle)

    def setUp(self):
        self.lib.reset()

    def snapshot(self):
        data = (C.c_ubyte * 2048)()
        length = self.lib.snapshot(data)
        return bytes(data[:length])

    def test_all_old_and_new_editor_backups_restore_exactly_on_cancel(self):
        for target, slots in enumerate((7, 5, 7, 4, 7, 2, 15, 25, 32, 1)):
            with self.subTest(target=target):
                self.lib.reset()
                before = self.snapshot()
                self.lib.begin(target)
                self.assertEqual(self.lib.slots(), slots)
                self.lib.edit(0)
                self.lib.edit(1)
                self.lib.edit(2)
                self.assertNotEqual(self.snapshot(), before)
                self.lib.finish(0)
                self.assertEqual(self.snapshot(), before)
                self.assertEqual((self.lib.editing(), self.lib.dirty()), (0, 0))

    def test_confirmed_edit_survives_later_canceled_edit_and_scratch_reuse(self):
        for target in range(10):
            with self.subTest(target=target):
                self.lib.reset()
                before = self.snapshot()
                self.lib.begin(target)
                self.lib.edit(2)
                self.lib.finish(1)
                saved = self.snapshot()
                self.assertNotEqual(saved, before)
                self.assertEqual(self.lib.dirty(), 1)
                self.lib.begin((target + 1) % 10)
                self.lib.edit(2)
                self.lib.finish(0)
                self.assertEqual(self.snapshot(), saved)
                self.assertEqual(self.lib.dirty(), 1)

    def test_native_current_defaults_and_backup_slices_remain_disjoint(self):
        self.lib.begin(6)
        self.assertEqual(self.lib.scratchCheck(), 0)
        self.lib.edit(2)
        self.assertEqual(self.lib.scratchCheck(), 0)
        self.lib.select(15)
        self.lib.edit(2)
        self.assertEqual(self.lib.scratchCheck(), 0)
        self.lib.finish(0)

    def test_attempt_to_open_other_editor_cannot_repoint_active_backup(self):
        before = self.snapshot()
        self.lib.begin(0)
        self.lib.edit(2)
        self.lib.begin(5)
        self.assertEqual(self.lib.slots(), 7)
        self.lib.finish(0)
        self.assertEqual(self.snapshot(), before)

    def test_independent_styles_roundtrip_and_render_exact_position(self):
        cfg = (C.c_ubyte * 128)()
        self.lib.stage(cfg)
        for display in range(3):
            offset = 8 + display * 36
            cfg[offset:offset+2] = (620 + display).to_bytes(2, "little")
            cfg[offset+2:offset+4] = (400 + display).to_bytes(2, "little")
            cfg[offset+4] = 110 + display
            cfg[offset+33:offset+36] = b"xyz"
        self.lib.adopt(cfg)
        saved = (C.c_ubyte * 128)()
        self.lib.stage(saved)
        for display in range(3):
            offset = 8 + display * 36
            self.assertEqual(bytes(saved[offset+33:offset+36]), b"\0\0\0")
            self.lib.draw(display, 0)
            self.assertEqual([self.lib.drawn(i) for i in (1,2,3)],
                             [620+display, 400+display, 110+display])
            self.assertEqual(self.lib.drawn(4), 120 + display * 20)
        before = bytes(saved)
        for offset in (0, 4, 6):
            invalid = (C.c_ubyte * 128).from_buffer_copy(before)
            invalid[offset] ^= 0x10
            self.lib.adopt(invalid)
            self.lib.stage(saved)
            self.assertEqual(bytes(saved), before)

    def test_only_exact_untouched_inherited_defaults_are_separated(self):
        for changed in (None, "x", "rgb"):
            with self.subTest(changed=changed):
                cfg = (C.c_ubyte * 128)()
                self.lib.stage(cfg)
                old = (300).to_bytes(2, "little") + (106).to_bytes(2, "little")
                old += bytes((90, 255, 0, 0, 0, 185, 100, 5)) + b"\xff" * 21
                for display in range(3):
                    offset = 8 + display * 36
                    cfg[offset:offset+33] = old
                    if changed == "x":
                        cfg[offset] += 1
                    if changed == "rgb":
                        cfg[offset+12+20] = 254
                self.lib.adopt(cfg)
                saved = (C.c_ubyte * 128)()
                self.lib.stage(saved)
                for display in range(3):
                    offset = 8 + display * 36
                    if changed:
                        self.assertEqual(bytes(saved[offset:offset+33]), bytes(cfg[offset:offset+33]))
                    else:
                        self.assertEqual(int.from_bytes(saved[offset+2:offset+4], "little"), (106,132,156)[display])
                        self.assertEqual(saved[offset+4], 70 if display == 2 else 90)
                        self.assertEqual(saved[offset+9], 128 if display == 2 else 185)
                        self.assertEqual(saved[offset+11], 2 if display == 2 else 5)

    def test_shared_old_display_dispatch_keeps_colour_bounds_and_null_guards(self):
        for display, count in enumerate((7, 5, 7)):
            for color in (-10, 0, count-1, count+10):
                with self.subTest(display=display, color=color):
                    self.lib.reset()
                    self.lib.drawOld(display, color, 0)
                    self.assertEqual(self.lib.drawn(0), 1)
                    self.assertEqual(self.lib.drawn(4),
                                     (30, 90, 60)[display] + max(0, min(color, count-1)))
                    for missing in (1, 2):
                        self.lib.drawOld(display, color, missing)
                        self.assertEqual(self.lib.drawn(0), 1)


if __name__ == "__main__":
    unittest.main()
