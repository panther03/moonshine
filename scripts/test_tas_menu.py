"""Exercise the actual TAS page's raw prompts and inline shortcut recorder."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_nested_menu_focus import function

ROOT = Path(__file__).resolve().parents[1]


class TasMenuTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        text = (ROOT / 'src/tas_menu.inc').read_text()
        text = text.replace(function(text, '    void draw(Menu *menu, int x, int y, int w, int h) override'), '')
        text = text[text.index('class TasProjectTab'):text.index('static_assert')]
        text = text.replace(' : public MenuTab', '').replace(' override', '').replace('private:', 'public:')
        menu_update = function((ROOT / 'src/menu.cpp').read_text(), 'void Menu::update(TMarioGamePad *pad)')
        raw = (ROOT / 'include/susamune/raw_prompt_input.hxx').read_text()
        raw = raw[raw.index('class RawPromptInput'):raw.index('#endif')]
        shim = r'''
#include "susamune/state_storage.h"
#include "susamune/binds_list.h"
#include "susamune/settings_list.h"
typedef unsigned char u8;typedef unsigned short u16;typedef unsigned u32;
#define ID(name,key) name,
enum BindId { SUSAMUNE_BIND_LIST(ID) BIND_COUNT };
enum SettingId { SUSAMUNE_SETTING_LIST(ID) SETTING_COUNT };
#undef ID
extern "C" void *memcpy(void*d,const void*s,unsigned long long n){for(unsigned i=0;i<n;++i)((volatile char*)d)[i]=((const char*)s)[i];return d;}
extern "C" unsigned long long strlen(const char*s){unsigned n=0;while(s[n])++n;return n;}
struct JUTGamePad {enum{A=0x100,B=0x200,X=0x400,Y=0x800,Z=0x10,START=0x1000};
 struct Status{u16 mButton;};static Status mPadStatus[1];};
JUTGamePad::Status JUTGamePad::mPadStatus[1];
struct TMarioGamePad {enum{CSTICK_UP=1,CSTICK_DOWN=2,CSTICK_LEFT=4,CSTICK_RIGHT=8};
 enum{DPAD_LEFT=1,DPAD_RIGHT=2,DPAD_DOWN=4,DPAD_UP=8,L=0x40,R=0x20,
 A=0x100,B=0x200,X=0x400,Y=0x800};
 struct {u32 mRapidInput;}mButtons;u32 nav;};
static int calls,lastAction,closeCount,bindTarget,cleared,confirmCount,cancelCount;
static bool overwrite,replacePrompt,phaseBusy,dirtyState;
static int saves,renames;static bool acceptSave;
static u16 closeMask=0x1800,previousButtons;
struct Settings { bool value=true,star=false; void cycle(SettingId,int){value=!value;}
 void toggleFavorite(SettingId){star=!star;} bool dirty(){return false;} }gSettings;
void hit(int action){++calls;lastAction=action;}
class TasProjectTab;
struct Menu {bool mShown=true;int mCurTab=0,mToastFrames=0,mCRepeatFrames=0;TasProjectTab*mTabs[1];
 u32 navigationInput(TMarioGamePad*p){return p->nav;}void hide(){++closeCount;mShown=false;}
 void toast(const char*){}void update(TMarioGamePad*);void pollSettingsSave(){}void requestSettingsSave(){}
 void switchTab(int){hit(99);}};
struct Binds {bool rec;bool recording(){return rec;}void cancelRecord(){rec=false;}
 void beginRecord(BindId id){rec=true;bindTarget=id;}void set(BindId id,u16 v){if(!v)cleared=id;}
 bool dirty(){return false;}bool wasPressedRaw(BindId){return closeMask&&JUTGamePad::mPadStatus[0].mButton==closeMask&&previousButtons!=closeMask;}}gBinds;
void updateAchievementBanner(){}bool rngControlInvalidatesIl(){return false;}
namespace WarpWheel {bool promptShown(){return false;}}
namespace StageTargets {void service(Menu*){}}
namespace LayoutProfiles {const char*poll(){return nullptr;}}
namespace MarioColors {bool dirty(){return false;}}
namespace FluddColors {bool dirty(){return false;}}
struct Display {bool dirty(){return false;}void update(){}}gInputDisplay,gMetadataDisplay,gQftDisplay,gCreationExtras;
struct StateManager {struct Info{u32 generation;};Info slotInfo(u32){return {11};}}manager,*gSavestateMgr=&manager;
int wrap(int v,int count){return (v+count)%count;}int clampi(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}
const char gCreationLettersLower[]="abcdefghijklmnopqrstuvwxyz012345";
const char gCreationLettersUpper[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ012345";
const char gCreationSymbols[]="!@#$%^&*()_+-=[]{};':,.<>/?0123456";
bool updateCreationKeyboardText(TMarioGamePad*,char*,u8&,u8,u8&,bool&,u8&);
namespace PracticeSession {
bool starting(){return false;}bool requestPauseToggle(bool){hit(6);return true;}
bool requestStep(bool){hit(7);return true;}const char*status(){return "status";}
}
namespace TasProject {
const char*status(){return "status";}const char*name(){return "TAS";}bool active(){return true;}
bool busy(){return phaseBusy;}bool dirty(){return dirtyState;}bool named(){return true;}
bool replacementNeeded(){return replacePrompt;}bool replacementAllowed(u32){return true;}
bool replace(u32,u32){replacePrompt=phaseBusy=false;hit(90);return true;}
void cancelReplacement(){replacePrompt=phaseBusy=false;}
bool checkpointOverwritePending(){return overwrite;}
bool confirmCheckpointOverwrite(bool accept){overwrite=phaseBusy=false;if(accept)++confirmCount;else ++cancelCount;return true;}
bool newProject(){hit(0);return true;}bool continueEditing(){hit(1);return true;}bool replay(){hit(2);return true;}
bool save(const char*){++saves;return acceptSave;}bool open(u32,u32){hit(4);return true;}
bool saveCheckpoint(u32 role){hit(10+role);overwrite=phaseBusy=true;return true;}
bool loadCheckpoint(u32 role){hit(20+role);return true;}
bool refresh(u32=0){return true;}bool rename(u32,u32,const char*){++renames;return acceptSave;}bool remove(u32,u32){return true;}
bool catalogReady(){return false;}const SusamuneStateCatalog&catalog(){static SusamuneStateCatalog c={};return c;}
}
'''
        body = r'''
static void reset(){calls=lastAction=closeCount=confirmCount=cancelCount=0;bindTarget=cleared=-1;
 overwrite=replacePrompt=phaseBusy=dirtyState=gBinds.rec=false;JUTGamePad::mPadStatus[0].mButton=0;
 gSettings.value=true;gSettings.star=false;closeMask=0x1800;previousButtons=0;saves=renames=0;acceptSave=true;}
static void press(TasProjectTab&t,u16 held,u32 nav=0){Menu m;TMarioGamePad p={};p.nav=nav;
 JUTGamePad::mPadStatus[0].mButton=held;t.update(&m,&p);}
extern "C" __declspec(dllexport) int route(int page,int row,int button,int*out){
 reset();TasProjectTab tab;tab.mPage=page;tab.mSel=row;press(tab,button);
 out[0]=lastAction;out[1]=closeCount;out[2]=bindTarget;out[3]=cleared;out[4]=tab.grabsInput();return calls;}
extern "C" __declspec(dllexport) int binding(int page,int row,int fourth,int*out){
 reset();TasProjectTab tab;tab.mPage=page;tab.mSel=row;press(tab,JUTGamePad::X);
 out[0]=tab.grabsInput();out[1]=bindTarget;
 press(tab,0);press(tab,0x260);gBinds.rec=false;press(tab,0x260|fourth);
 out[2]=calls;out[3]=closeCount;out[4]=tab.mSel;out[5]=gBinds.rec;
 press(tab,0);press(tab,JUTGamePad::A);return calls;}
extern "C" __declspec(dllexport) int overwriteCase(int shortcut,int cancel){
 reset();TasProjectTab tab;
 if(shortcut){overwrite=phaseBusy=true;JUTGamePad::mPadStatus[0].mButton=JUTGamePad::A;tab.showCheckpointPrompt();}
 else {tab.mPage=tab.CHECKPOINTS;tab.mSel=1;press(tab,JUTGamePad::A);}
 for(int i=0;i<9;++i)press(tab,JUTGamePad::A);
 if(confirmCount||cancelCount||!overwrite||!tab.grabsInput())return 1;
 press(tab,0);press(tab,cancel?JUTGamePad::B:JUTGamePad::A);
 if(overwrite||confirmCount!=(cancel?0:1)||cancelCount!=(cancel?1:0))return 2;
 return 0;
}
extern "C" __declspec(dllexport) int cancelBinding(){
 reset();TasProjectTab tab;tab.mSel=7;press(tab,JUTGamePad::X);
 press(tab,JUTGamePad::A,TMarioGamePad::CSTICK_LEFT);
 press(tab,JUTGamePad::A);if(gBinds.rec||calls)return 1;
 press(tab,0);press(tab,JUTGamePad::A);return calls==1&&lastAction==7?0:2;
}
extern "C" __declspec(dllexport) int bannerCase(int nav){
 reset();TasProjectTab tab;tab.mSel=7;press(tab,0,TMarioGamePad::CSTICK_DOWN);
 if(tab.mSel!=8||!tab.favoriteHint())return 1;
 press(tab,nav?0:JUTGamePad::A,nav?TMarioGamePad::CSTICK_RIGHT:0);
 if(gSettings.value||calls||closeCount)return 2;
 press(tab,0);press(tab,JUTGamePad::X);
 if(!gSettings.star||gBinds.rec||bindTarget!=-1)return 3;
 press(tab,0,TMarioGamePad::CSTICK_DOWN);
 return tab.mSel==0&&!tab.favoriteHint()?0:4;
}
extern "C" __declspec(dllexport) int closePage(int page,int mask,int busy,int recording,int*out){
 reset();TasProjectTab tab;tab.mPage=page;tab.mSel=page==tab.CHECKPOINTS?1:7;
 Menu menu;menu.mTabs[0]=&tab;TMarioGamePad pad={};closeMask=(u16)mask;
 phaseBusy=busy;tab.mBinding=recording!=0;gBinds.rec=recording==1;
 JUTGamePad::mPadStatus[0].mButton=(u16)mask;pad.mButtons.mRapidInput=mask;
 menu.update(&pad);out[0]=menu.mShown;out[1]=calls;out[2]=confirmCount;out[3]=cancelCount;
 out[4]=tab.mPage;out[5]=gBinds.rec;
 if(recording){previousButtons=(u16)mask;gBinds.rec=false;menu.update(&pad);
  out[6]=menu.mShown;JUTGamePad::mPadStatus[0].mButton=0;pad.mButtons.mRapidInput=0;
  menu.update(&pad);previousButtons=0;JUTGamePad::mPadStatus[0].mButton=(u16)mask;
  pad.mButtons.mRapidInput=mask;menu.update(&pad);out[7]=menu.mShown;}
 return 0;
}
extern "C" __declspec(dllexport) void nameClose(int mask,int rename,int accept,int*out){
 reset();TasProjectTab tab;memcpy(tab.mName,"Name",5);tab.beginKeyboard();tab.focus();
 tab.mRename=rename!=0;acceptSave=accept!=0;
 Menu menu;menu.mTabs[0]=&tab;TMarioGamePad pad={};closeMask=(u16)mask;
 JUTGamePad::mPadStatus[0].mButton=(u16)mask;pad.mButtons.mRapidInput=mask;
 menu.update(&pad);
 out[0]=menu.mShown;out[1]=tab.mPage;out[2]=tab.mCursor;out[3]=tab.mLength;
 out[4]=tab.mKeyPage;out[5]=tab.mUpper;out[6]=saves;out[7]=renames;out[8]=calls;
 previousButtons=(u16)mask;menu.update(&pad);out[9]=menu.mShown;
 JUTGamePad::mPadStatus[0].mButton=0;pad.mButtons.mRapidInput=0;menu.update(&pad);
 previousButtons=0;JUTGamePad::mPadStatus[0].mButton=(u16)mask;
 pad.mButtons.mRapidInput=mask;menu.update(&pad);out[10]=menu.mShown;
}
'''
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        source = Path(cls.tmp.name) / 'menu.cpp'
        keyboard = function((ROOT / 'src/creation_extras.cpp').read_text(), 'bool updateCreationKeyboardButtons(')
        keyboard += function((ROOT / 'src/creation_extras.cpp').read_text(),
                            'bool updateCreationKeyboardText(')
        source.write_text(shim + keyboard + raw + text + menu_update + body)
        dll = source.with_suffix('.dll')
        result = subprocess.run([str(ROOT/'toolchain/clang++.exe'), '--target=x86_64-pc-windows-msvc',
            '-shared','-nostdlib','-fuse-ld=lld','-Wl,/noentry','-O2','-std=c++17',
            '-I',str(ROOT/'include'),str(source),'-o',str(dll)],capture_output=True,text=True)
        if result.returncode: raise AssertionError(result.stderr)
        cls.lib = C.CDLL(str(dll))
        cls.addClassCleanup(lambda:C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def test_nested_checkpoint_name_is_localized_before_formatting(self):
        text = (ROOT / 'src/tas_menu.inc').read_text()
        self.assertIn('JapaneseUi::text(TasProject::roleName(TasProject::pendingCheckpointRole()))', text)

    def test_checkpoint_in_another_area_is_not_presented_as_empty(self):
        text = (ROOT / 'src/tas_menu.inc').read_text()
        self.assertIn('if (cp.present && !cp.loadableHere) memcpy(value, "Other area", 11);', text)
        self.assertIn('else if (cp.present) snprintf(value, sizeof(value), "%lu frames", cp.frames);', text)
        self.assertIn('else memcpy(value, "Empty", 6);', text)
        self.assertIn('PracticeSession::recordedFrames(), PracticeSession::capacityFrames()', text)
        self.assertIn('Its recording and checkpoint files will be deleted.', text)

    def test_each_inline_shortcut_edits_and_clears_without_action(self):
        rows = [(0,1,39),(0,2,40),(0,6,23),(0,7,24)] + [(1,i,34+i) for i in range(5)]
        for page,row,bind in rows:
            for button in (0x400,0x10):
                out=(C.c_int*5)()
                self.assertEqual(self.lib.route(page,row,button,out),0)
                self.assertEqual(out[2 if button==0x400 else 3],bind)

    def test_actions_stay_reachable(self):
        for page,row,action,closes in [(0,1,1,1),(0,2,2,1),(0,6,6,1),(0,7,7,1),
                (1,0,20,1),(1,1,11,0),(1,2,21,0),(1,3,12,0),(1,4,22,0)]:
            out=(C.c_int*5)()
            self.assertEqual(self.lib.route(page,row,0x100,out),1)
            self.assertEqual(list(out)[:2],[action,closes])

    def test_banner_toggle_and_shine_are_reachable_without_triggering_an_action(self):
        for nav in (0, 1):
            self.assertEqual(self.lib.bannerCase(nav), 0)

    def test_fourth_recorded_button_cannot_activate_rebind_or_close(self):
        for page,row in [(0,7),(1,1)]:
            for button in (0x100,0x400):
                out=(C.c_int*6)()
                self.assertEqual(self.lib.binding(page,row,button,out),1)
                self.assertEqual(out[0],1)
                self.assertEqual(list(out)[2:],[0,0,row,0])

    def test_cstick_cancels_recording_without_leaking_held_action(self):
        self.assertEqual(self.lib.cancelBinding(),0)

    def test_menu_and_shortcut_confirmations_require_a_new_press(self):
        for shortcut in (0,1):
            for cancel in (0,1):
                self.assertEqual(self.lib.overwriteCase(shortcut,cancel),0)

    def test_close_shortcut_exits_non_naming_pages_without_running_its_action(self):
        for page in (0, 1, 2, 4, 5):
            for mask in (0x1800, 0x140, 0x60):
                for busy in (0, 1):
                    with self.subTest(page=page, mask=mask, busy=busy):
                        out = (C.c_int * 8)()
                        self.lib.closePage(page, mask, busy, 0, out)
                        self.assertEqual(list(out)[:4], [0, 0, 0, 0])
                        self.assertEqual(out[4], page)

    def test_keyboard_owns_close_bind_for_each_typing_control(self):
        # Menu::update and the real shared keyboard handle these inputs together.
        for mask, cursor, length, page, upper in (
                (1,31,4,0,0), (2,1,4,0,0), (4,8,4,0,0), (8,24,4,0,0),
                (0x40,0,4,1,0), (0x20,0,4,1,0), (0x100,0,5,0,0),
                (0x200,0,3,0,0), (0x400,0,5,0,0), (0x800,0,4,0,1),
                (0x10,0,0,0,0)):
            with self.subTest(mask=mask):
                out=(C.c_int*11)()
                self.lib.nameClose(mask,0,1,out)
                self.assertEqual(list(out)[:9], [1,3,cursor,length,page,upper,0,0,0])
                self.assertEqual(list(out)[9:], [1,1])

    def test_keyboard_save_cancel_and_failed_save_keep_final_input_owned(self):
        for rename in (0,1):
            for cancel in (0,1):
                for accept in (0,1):
                    with self.subTest(rename=rename,cancel=cancel,accept=accept):
                        mask=0x1000 | (0x400 if cancel else 0)
                        out=(C.c_int*11)()
                        self.lib.nameClose(mask,rename,accept,out)
                        finished=cancel or accept
                        self.assertEqual(out[0],1)  # No close on commit/cancel frame.
                        self.assertEqual(out[1],0 if finished else 3)
                        self.assertEqual(list(out)[6:9],
                                         [int(not cancel and not rename),
                                          int(not cancel and rename),0])
                        self.assertEqual(out[9],1)  # Holding cannot close it either.
                        self.assertEqual(out[10],0 if finished else 1)

    def test_busy_named_page_keeps_close_bind_owned(self):
        for mask in (4,0x1000,0x1800):
            out=(C.c_int*8)()
            self.lib.closePage(3,mask,1,0,out)
            self.assertEqual(list(out)[:5],[1,0,0,0,3])

    def test_inline_recorder_and_its_commit_frame_keep_close_combo_until_fresh_press(self):
        for page in (0, 1):
            for recording in (1, 2):
                out = (C.c_int * 8)()
                self.lib.closePage(page, 0x1800, 0, recording, out)
                self.assertEqual(list(out)[:4], [1, 0, 0, 0])
                self.assertEqual(out[6], 1)
                self.assertEqual(out[7], 0)


if __name__ == '__main__': unittest.main()
