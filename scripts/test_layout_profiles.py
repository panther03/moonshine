"""Exercise the real layout-profile client and menu with isolated storage."""

import ctypes as C
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_practice_tape import function_source

ROOT = Path(__file__).resolve().parents[1]


class LayoutProfileClientTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="moonshine-layout-client-")
        cls.addClassCleanup(cls.temp.cleanup)
        source = (ROOT / "src/layout_profiles.cpp").read_text()
        source = re.sub(r'^#include[^\n]+\n', '', source, flags=re.M)
        pairs = [('input', 'inputStyle'), ('metadata', 'metadataStyle'), ('qft',)]
        fixture = r'''
#include "susamune/layout_profile.h"
#include "susamune/settings_list.h"
typedef unsigned char u8;typedef unsigned short u16;typedef unsigned int u32;
#define ENUM(id,key) id,
enum SettingId{SUSAMUNE_SETTING_LIST(ENUM) SETTING_COUNT};
#undef ENUM
enum{SETTINGS_SAVE_IDLE,SETTINGS_SAVE_PENDING};
extern "C" void *memset(void*d,int v,__SIZE_TYPE__ n){u8*p=(u8*)d;while(n--)*p++=(u8)v;return d;}
extern "C" void *memcpy(void*d,const void*s,__SIZE_TYPE__ n){u8*p=(u8*)d;const u8*q=(const u8*)s;while(n--)*p++=*q++;return d;}
extern "C" char *strncpy(char*d,const char*s,__SIZE_TYPE__ n){char*p=d;while(n--){*p++=*s;if(*s)++s;}return d;}
static SusamuneCfg testCfg;
static MoonshineLayoutMailbox mailbox;
static MoonshineLayoutPayload live;
static const char *borrowedFormat;
static u32 edits,applied,scheduled,invalidations,flushes;
static bool inputVisible,lockBlocked;
struct Settings{u8 values[SETTING_COUNT];u32 pending;bool dirty;
u8 get(SettingId id){return values[id];}void set(SettingId id,u8 v){values[id]=v;}
u32 saveState(){return pending;}void markDirty(){dirty=true;}}gSettings;
struct Menu{void scheduleSettingsSave(){++scheduled;}}menu,*gMenu=&menu;
namespace EmulatorPersistence{SusamuneCfg*lock(){return lockBlocked?nullptr:&testCfg;}void unlock(){}}
static void DCInvalidateRange(const void*,u32){++invalidations;}
static void DCFlushRange(const void*,u32){++flushes;}
#undef MOONSHINE_LAYOUT_PPC_PTR
#define MOONSHINE_LAYOUT_PPC_PTR (&mailbox)
#undef SUSAMUNE_CFG_PPC_PTR
#define SUSAMUNE_CFG_PPC_PTR (&testCfg)
'''
        types = dict(re.findall(r'struct (Susamune\w+) (\w+);', (ROOT/'include/susamune/layout_profile.h').read_text()))
        by_field = {v:k for k,v in types.items()}
        for index, (name, members) in enumerate((('InputDisplay', pairs[0]), ('MetadataDisplay', pairs[1]),
                                                ('QftDisplay', pairs[2]), ('CreationExtras', ('creation','wallkick','movement','nativeTimer','practiceDisplays')))):
            fixture += f'struct {name}{{bool editing(){{return edits&(1u<<{index});}}\n'
            if name == 'InputDisplay':
                fixture += 'bool visible(){return inputVisible;}\n'
            for j, field in enumerate(members):
                suffix = '' if j == 0 else 'Style' if j == 1 and name != 'CreationExtras' else field[0].upper()+field[1:]
                stage_name = 'stage'+suffix+'Into'
                adopt_name = 'adopt'+suffix
                fixture += f'void {stage_name}(volatile {by_field[field]}*out){{memcpy((void*)out,&live.{field},sizeof(*out));}}\n'
                extra = 'borrowedFormat=(const char*)in->format;' if field=='metadata' else ''
                fixture += f'void {adopt_name}(const volatile {by_field[field]}*in){{memcpy(&live.{field},(const void*)in,sizeof(*in));++applied;{extra}}}\n'
            fixture += '}g'+name+';\n'
        for index, field in enumerate(('mario','fludd'),4):
            fixture += f'namespace {field.capitalize()}Colors{{bool editing(){{return edits&(1u<<{index});}}'
            fixture += f'void stageInto(volatile {by_field[field]}*out){{memcpy((void*)out,&live.{field},sizeof(*out));}}'
            fixture += f'void adopt(const volatile {by_field[field]}*in){{memcpy(&live.{field},(const void*)in,sizeof(*in));++applied;}}}}\n'
        fixture += '#include "susamune/layout_profiles.hxx"\n'
        # Avoid the concrete Settings class: the client uses the same public operations above.
        fixture = fixture.replace('#include "susamune/layout_profiles.hxx"', '''namespace LayoutProfiles{
bool available();bool busy();bool present(u32);bool damaged(u32);const char*name(u32);
u32 generation(u32);bool refresh();bool save(u32,const char*,u32);bool load(u32);
const char*poll();bool layoutSetting(SettingId);void capture(MoonshineLayoutPayload*);
bool apply(const MoonshineLayoutPayload&);}
''')
        fixture += source
        fixture += r'''
#define API extern "C" __declspec(dllexport)
API void setup(u32 fault){
 memset(&mailbox,0,sizeof(mailbox));memset(&testCfg,0,sizeof(testCfg));memset(&live,0,sizeof(live));
 testCfg.magic=SUSAMUNE_CFG_MAGIC;testCfg.version=SUSAMUNE_CFG_VERSION;testCfg.flags=MOONSHINE_LAYOUT_CFG_FLAG;
 mailbox.magic=MOONSHINE_LAYOUT_MAILBOX_MAGIC;mailbox.version=MOONSHINE_LAYOUT_MAILBOX_VERSION;
 if(fault==1)testCfg.magic=0;if(fault==2)++testCfg.version;if(fault==3)testCfg.flags=0;
 if(fault==4)mailbox.magic=0;if(fault==5)++mailbox.version;
 for(u32 i=0;i<SETTING_COUNT;i++)gSettings.values[i]=(u8)(i+1);
 gSettings.pending=0;gSettings.dirty=false;inputVisible=false;lockBlocked=false;
 edits=applied=scheduled=invalidations=flushes=0;borrowedFormat=nullptr;
 LayoutProfiles::sOperation=0;LayoutProfiles::sSequence=0;
}
API void seed(u32 slot,u32 gen){if(slot>=5)return;mailbox.presentMask|=1u<<slot;
 mailbox.generations[slot]=gen;strncpy(mailbox.names[slot],"Named profile",15);}
API void fill(u32 value){memset(&live,(int)value,sizeof(live));live.input.startVisible=1;
 strncpy(live.metadata.format,"Persistent format",sizeof(live.metadata.format));}
API void visible(u32 v){inputVisible=v!=0;}
API u32 request(u32 op,u32 slot,u32 expected){
 return op==1?LayoutProfiles::refresh():op==2?LayoutProfiles::save(slot,"Named profile",expected):LayoutProfiles::load(slot);}
API void reply(u32 status,u32 gen,u32 corrupt,u32 wrongSeq){
 mailbox.status=status;mailbox.ackSeq=mailbox.requestSeq+wrongSeq;
 if(mailbox.operation==MOONSHINE_LAYOUT_LOAD){
  MoonshineLayoutFile &file=mailbox.file;memset(&file,0,sizeof(file));
  file.magic=MOONSHINE_LAYOUT_MAGIC;file.version=MOONSHINE_LAYOUT_VERSION;
  file.bytes=sizeof(file);file.generation=gen;strncpy(file.name,"Loaded",15);
  memcpy(&file.layout,&live,sizeof(live));
  for(u32 i=0;i<SETTING_COUNT;i++)file.layout.settings[i]=LayoutProfiles::layoutSetting((SettingId)i)?7:99;
  file.checksum=MoonshineLayoutChecksum(&file);if(corrupt)file.checksum^=1;
 }
}
API const char*poll(){return LayoutProfiles::poll();}
API void block(u32 edit,u32 pending,u32 locked){edits=edit;gSettings.pending=pending;lockBlocked=locked;}
API u32 get(u32 key){switch(key){case 0:return LayoutProfiles::available();case 1:return LayoutProfiles::busy();
 case 2:return applied;case 3:return scheduled;case 4:return gSettings.dirty;
 case 5:return mailbox.file.layout.input.startVisible;case 6:return mailbox.expectedGeneration;
 case 7:return mailbox.file.generation;case 8:return mailbox.requestSeq;
 case 9:return flushes;case 10:return invalidations;case 11:return sizeof(live);
 case 12:return sizeof(mailbox.file);case 13:return borrowedFormat==testCfg.metadataDisplay.format;}
 return 0;}
API u32 setting(u32 i){return i<SETTING_COUNT?gSettings.values[i]:0;}
API u32 setting_count(){return SETTING_COUNT;}
API u32 included(u32 i){return i<SETTING_COUNT&&LayoutProfiles::layoutSetting((SettingId)i);}
API u32 map_value(u32 i){switch(i){
 case 0:return MOONSHINE_LAYOUT_MAILBOX_OFFSET;
 case 1:return MOONSHINE_LAYOUT_MAILBOX_SIZE;
 case 2:return (SUSAMUNE_MEM2_MENU_RUNTIME_PPC_BASE&0xffffu)+SUSAMUNE_FOXTROT_MENU_RUNTIME_SIZE;
 case 3:return SUSAMUNE_SPLIT_STATS_CFG_OFFSET;
 case 4:return sizeof(MoonshineLayoutMailbox);
 case 5:return __builtin_offsetof(MoonshineLayoutMailbox,file);
 case 6:return MOONSHINE_LAYOUT_CFG_FLAG;
 }return 0;}
API const void*file_bytes(){return &mailbox.file;}
API const void*live_bytes(){return &live;}
API const char*format(){return borrowedFormat;}
API void overwrite_payload(){memset(&mailbox.file,0xcc,sizeof(mailbox.file));}
API void set_sequence(u32 sequence){mailbox.requestSeq=mailbox.ackSeq=sequence;}
'''
        cls.libs=[]
        for emulator in (0,1):
            path=Path(cls.temp.name)/f'profiles{emulator}.cpp';path.write_text(fixture)
            result=subprocess.run([str(ROOT/'toolchain/clang++.exe'),'--target=x86_64-pc-windows-msvc',
                '-shared','-nostdlib','-fuse-ld=lld','-Wl,/noentry','-O2','-fno-builtin','-mno-stack-arg-probe',
                f'-DIS_EMULATOR={emulator}','-I',str(ROOT/'include'),str(path),'-o',str(path.with_suffix('.dll'))],capture_output=True,text=True)
            if result.returncode:raise RuntimeError(result.stdout+result.stderr)
            lib=C.CDLL(str(path.with_suffix('.dll')));cls.libs.append(lib)
            for name in ('file_bytes','live_bytes'):getattr(lib,name).restype=C.c_void_p
            for name in ('poll','format'):getattr(lib,name).restype=C.c_char_p
            lib.get.restype=C.c_uint
            cls.addClassCleanup(lambda h=lib._handle:C.windll.kernel32.FreeLibrary(C.c_void_p(h)))

    def test_old_launcher_and_unknown_mailboxes_refuse_all_requests(self):
        for emulator,lib in enumerate(self.libs):
            for fault in (range(4,6) if emulator else range(1,6)):
                lib.setup(fault);before=C.string_at(lib.file_bytes(),lib.get(12))
                for op in range(1,4):self.assertEqual(lib.request(op,0,0),0)
                self.assertEqual(lib.get(0),0)
                self.assertEqual(C.string_at(lib.file_bytes(),lib.get(12)),before)

    def test_capture_keeps_all_visual_payloads_and_current_input_visibility(self):
        for lib in self.libs:
            lib.setup(0);lib.fill(0x42);lib.visible(0)
            self.assertEqual(lib.request(2,4,0),1)
            self.assertEqual(lib.get(5),0)
            data=C.string_at(lib.file_bytes(),lib.get(12))[32:]
            original=C.string_at(lib.live_bytes(),lib.get(11))
            # Only setting allowlist, reserved header and current visibility may differ.
            diff=[i for i,(a,b) in enumerate(zip(data,original)) if a!=b]
            self.assertTrue(all(i<144 or i==144+10 for i in diff),diff)

    def test_pending_request_owns_buffer_until_matching_ack_and_blocks_all_mutations(self):
        lib=self.libs[0];lib.setup(0);lib.fill(0x42);self.assertEqual(lib.request(2,4,23),1)
        before=C.string_at(lib.file_bytes(),lib.get(12))
        for op in range(1,4):self.assertEqual(lib.request(op,1,0),0)
        self.assertEqual(C.string_at(lib.file_bytes(),lib.get(12)),before)
        lib.reply(0,0,0,1);self.assertIsNone(lib.poll());self.assertEqual(lib.get(1),1)
        lib.reply(0,0,0,0);self.assertEqual(lib.poll(),b'Layout profile saved');self.assertEqual(lib.get(1),0)
        self.assertEqual(lib.get(7),24)

    def test_load_checks_generation_checksum_and_error_before_any_apply(self):
        for status,gen,corrupt in ((0,9,0),(0,8,1),(0x10002,8,0),(5,8,0)):
            for lib in self.libs:
                lib.setup(0);lib.fill(0x31);lib.seed(0,8);self.assertEqual(lib.request(3,0,0),1)
                before=C.string_at(lib.live_bytes(),lib.get(11));lib.reply(status,gen,corrupt,0)
                self.assertIsNotNone(lib.poll())
                self.assertEqual([lib.get(i) for i in (1,2,3,4)],[0,0,0,0])
                self.assertEqual(C.string_at(lib.live_bytes(),lib.get(11)),before)

    def test_apply_waits_for_editors_and_pending_settings_then_persists_after_menu_close(self):
        for lib in self.libs:
            for edit,pending in tuple((1<<i,0) for i in range(6))+((0,1),):
                lib.setup(0);lib.fill(0x21);lib.seed(2,7);lib.request(3,2,0);lib.reply(0,7,0,0)
                lib.block(edit,pending,0);self.assertIsNone(lib.poll());self.assertEqual(lib.get(2),0)
                lib.block(0,0,0);self.assertEqual(lib.poll(),b'Layout profile applied')
                self.assertEqual([lib.get(i) for i in (1,2,3,4,13)],[0,12,1,1,1])
                self.assertIsNone(lib.poll());self.assertEqual(lib.get(3),1)

    def test_dolphin_failed_lock_preserves_layout_and_retries_once_lock_returns(self):
        lib=self.libs[1];lib.setup(0);lib.fill(0x33);lib.seed(1,3);lib.request(3,1,0);lib.reply(0,3,0,0)
        lib.block(0,0,1);before=C.string_at(lib.live_bytes(),lib.get(11))
        self.assertIsNone(lib.poll());self.assertEqual(C.string_at(lib.live_bytes(),lib.get(11)),before)
        lib.block(0,0,0);self.assertEqual(lib.poll(),b'Layout profile applied')

    def test_apply_changes_only_allowed_settings_and_owns_format_after_next_transfer(self):
        for lib in self.libs:
            lib.setup(0);lib.fill(0x33);lib.seed(1,3);lib.request(3,1,0);lib.reply(0,3,0,0);lib.poll()
            for i in range(lib.setting_count()):self.assertEqual(lib.setting(i),7 if lib.included(i) else (i+1)&255)
            self.assertEqual(lib.format(),b'Persistent format');lib.overwrite_payload()
            self.assertEqual(lib.format(),b'Persistent format')

    def test_hud_visibility_is_saved_without_gameplay_or_session_controls(self):
        keys=re.findall(r'X\((SETTING_\w+),', (ROOT/'include/susamune/settings_list.h').read_text())
        visible=('ACHIEVEMENT_NOTIFICATIONS','SHOW_BGM_SLOTS','RESTART_QUEUED_FEEDBACK',
                 'GB_SKIP_DISPLAY','JUMP_DISPLAY','TIMER_SUNSHINE_VISIBILITY','TIMER_QFT_VISIBILITY',
                 'GHOST_DISPLAY','GHOST_INPUTS','TAS_BANNER')
        controls=('FAST_TEXT','SAVE_RNG_STATE','FLUDD_SECRETS','PATTERN_SELECTOR','ATTEMPT_COUNTER',
                  'STREAK_AUTO_RESET','ILING_RECORDING','PB_GHOST_SAVE_POLICY','GHOST_LAST_SUCCESS',
                  'FREE_CAMERA_SPEED','FREE_CAMERA_SMOOTHING','MUTE_BGM','TIMER_FREEZE_DURATION')
        for lib in self.libs:
            for key in visible:self.assertTrue(lib.included(keys.index('SETTING_'+key)),key)
            for key in controls:self.assertFalse(lib.included(keys.index('SETTING_'+key)),key)

    def test_mailbox_uses_only_the_checked_config_gap_and_a_new_capability(self):
        header=(ROOT/'include/susamune/susamune_cfg.h').read_text()
        flags=[int(value,16) for value in re.findall(r'#define SUSAMUNE_CFG_FLAG_\w+ (0x[0-9a-fA-F]+)u',header)]
        for lib in self.libs:
            start,size,previous,next_,used,payload,flag=[lib.map_value(i) for i in range(7)]
            self.assertEqual((start,size,payload),(0x6800,0x1000,160))
            self.assertLessEqual(previous,start)
            self.assertLessEqual(start+size,next_)
            self.assertLessEqual(used,size)
            self.assertEqual(flag,0x800000)
            self.assertTrue(all((flag&other)==0 for other in flags))

    def test_five_slots_and_generation_sequence_wrap_are_bounded(self):
        lib=self.libs[0]
        for slot in (0,4):
            lib.setup(0);lib.set_sequence(0xffffffff);self.assertEqual(lib.request(2,slot,0xffffffff),1)
            self.assertEqual([lib.get(i) for i in (6,7,8)],[0xffffffff,1,1])
        lib.setup(0)
        for slot in (5,0xffffffff):self.assertEqual(lib.request(2,slot,0),0)


class LayoutProfileMenuTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='moonshine-layout-menu-')
        cls.addClassCleanup(cls.temp.cleanup)
        menu=(ROOT/'src/menu.cpp').read_text()
        tab=menu[menu.index('class LayoutProfilesTab final'):menu.index('class CreationTab final')]
        raw=(ROOT/'include/susamune/raw_prompt_input.hxx').read_text()
        raw=raw[raw.index('class RawPromptInput {'):raw.index('#endif')]
        keyboard_source=(ROOT/'src/creation_extras.cpp').read_text()
        keyboard='\n'.join(re.findall(r'^const char gCreation\w+\[[^\]]*\] = [^\n]+;', keyboard_source, re.M))
        keyboard+='\n'+function_source(ROOT/'src/creation_extras.cpp', 'bool updateCreationKeyboardButtons(')
        keyboard+='\n'+function_source(ROOT/'src/creation_extras.cpp', 'bool updateCreationKeyboardText(')
        bind_raw='\n'.join(function_source(ROOT/'include/susamune/binds.hxx', signature)
                           for signature in ('bool wasPressedRaw(', 'bool wasPressedSubsetRaw('))
        main=(ROOT/'src/main.cpp').read_text()
        # Execute the production pre-direct menu ownership and Pause/Step gate.
        main_gate='\n'.join(re.search(pattern, main, re.S).group(0) for pattern in (
            r'const bool menuOpenBeforeDirect = [^;]+;',
            r'bool menuOwnsRetailPad = [^;]+;',
            r'bool practiceModal = [^;]+;'))
        main_gate+='\n'+function_source(ROOT/'src/main.cpp', 'if (sessionResultBeforeDirect || menuOwnsRetailPad)')
        main_gate+='\n'+re.search(r'PatternSelector::update\([^;]+;', main, re.S).group(0)
        main_gate+='\n'+main[main.index('bool practiceStepConsumed = false;'):
                             main.index('        if (gBinds.wasPressed(BIND_FREE_CAMERA))')]+ '\n}\n'
        classic=function_source(ROOT/'src/warp_wheel.cpp', 'bool updateClassicInstant(')
        # Stop at the dispatch boundary; the actual release latch runs unchanged.
        classic=classic[:classic.index('    LevelWarp::Dest dest;')]+'return true;\n}\n'
        fixture=r'''
typedef unsigned char u8;typedef unsigned short u16;typedef unsigned int u32;
enum{MOONSHINE_LAYOUT_COUNT=5,MOONSHINE_LAYOUT_NAME_SIZE=16,ROW_H=22,FOOT_SZ=12};
enum BindId{BIND_MENU_TOGGLE,BIND_PRACTICE_PAUSE,BIND_PRACTICE_STEP,
BIND_PRACTICE_SPIN_CW,BIND_PRACTICE_SPIN_CCW,BIND_COUNT};
#define SUSAMUNE_GLYPH_A "A"
#define SUSAMUNE_GLYPH_B "B"
#define SUSAMUNE_GLYPH_Y "Y"
struct JUTGamePad{enum{A=1,B=2,Y=4,START=8,Z=16,L=256,R=512,DPAD_UP=8192};struct Pad{u16 mButton;};static Pad mPadStatus[1];};
JUTGamePad::Pad JUTGamePad::mPadStatus[1];
struct TMarioGamePad{enum{A=1,B=2,Y=4,X=32,CSTICK_UP=64,CSTICK_DOWN=128,L=256,R=512,
DPAD_DOWN=1024,DPAD_LEFT=2048,DPAD_RIGHT=4096,DPAD_UP=8192};struct{u32 mInput,mRapidInput;}mButtons;};
static u32 nav,tabSwitches,pauseRequests,stepRequests,classicDispatches;static const char *toast;
class MenuTab;
struct Menu{u32 navigationInput(TMarioGamePad*){return nav;}void toast(const char*t){::toast=t;}
void fillBox(int,int,int,int,int){}void drawText(const char*,int,int,int,int,int){}
void update(TMarioGamePad*);bool suppressesBinds()const;bool shown()const{return mShown;}
void pollSettingsSave(){}void requestSettingsSave(){}void switchTab(int){++tabSwitches;}
bool mShown;int mCurTab,mToastFrames,mCRepeatFrames;MenuTab*mTabs[1];};
struct Binds{u16 mMask[BIND_COUNT],mHeld,mPrevHeld;bool mRecSilent;
bool dirty(){return false;}bool recording(){return false;}
void suppressUntilRelease(){mRecSilent=true;}
void sample(){mPrevHeld=mHeld;mHeld=JUTGamePad::mPadStatus[0].mButton;if(!mHeld)mRecSilent=false;}
bool wasPressedPracticeRaw(BindId)const;
''' + bind_raw + r'''
}gBinds;
''' + function_source(ROOT/'src/binds.cpp', 'bool Binds::wasPressedPracticeRaw(') + r'''
struct Appearance{bool dirty(){return false;}void update(){}}gSettings,gInputDisplay,gMetadataDisplay,gQftDisplay,gCreationExtras;
namespace MarioColors{bool dirty(){return false;}}
namespace FluddColors{bool dirty(){return false;}}
namespace WarpWheel{bool promptPending(){return false;}bool promptShown(){return false;}
bool sClassicInstantHeld,sClassicInstantPending,sClassicInstantSuppressed;
const u16 kInstantBase=JUTGamePad::B|JUTGamePad::DPAD_UP;
''' + function_source(ROOT/'src/warp_wheel.cpp', 'void suppressClassicInstantUntilRelease()') + classic + r'''
}
namespace PatternSelector{bool inputAllowed;void update(bool allowInput){inputAllowed=allowInput;}}
namespace StageTargets{void service(Menu*){}}
namespace PracticeSession{bool requestStep(){++stepRequests;return true;}void requestPauseToggle(){++pauseRequests;}}
void updateAchievementBanner(){}
bool rngControlInvalidatesIl(){return false;}
int wrap(int x,int n){return (x+n)%n;}int cPanel(){return 0;}int cRowSel(){return 0;}int cRow(){return 0;}int cFooter(){return 0;}
extern "C" __SIZE_TYPE__ strlen(const char*p){__SIZE_TYPE__ n=0;while(p[n])++n;return n;}
extern "C" int snprintf(char*d,__SIZE_TYPE__ n,const char*,...){const char*p="Name";int i=0;while(p[i]&&i+1<n){d[i]=p[i];++i;}if(n)d[i]=0;return i;}
''' + keyboard + r'''
void drawCreationKeyboard(Menu*,const char*,const char*,u8,bool,u8){}
void drawValueRow(Menu*,int,int,int,const char*,const char*,bool,bool,bool){}
void drawHelpLine(Menu*,int,int,int,int,const char*){}
class MenuTab{public:virtual const char*title()const=0;virtual const char*summary()const=0;
virtual bool grabsInput()const=0;virtual bool suppressesBinds()const=0;
virtual bool fullScreen()const=0;virtual void focus()=0;virtual bool back()=0;
virtual void update(Menu*,TMarioGamePad*)=0;virtual void draw(Menu*,int,int,int,int)=0;};
namespace LayoutProfiles{
bool isBusy,exists;u32 currentGeneration,savedGeneration,saves,loads,refreshes,savedSlot;
bool available(){return true;}bool busy(){return isBusy;}bool present(u32){return exists;}
bool damaged(u32){return false;}u32 generation(u32){return exists?currentGeneration:0;}
const char*name(u32){return exists?"Existing":"";}bool refresh(){++refreshes;return true;}
const char*poll(){return nullptr;}
bool save(u32 slot,const char*,u32 expected){++saves;savedSlot=slot;savedGeneration=expected;return true;}
bool load(u32){++loads;return exists;}}
''' + function_source(ROOT/'src/menu.cpp', 'void drawFooterText(') + raw + '\n#define private public\n' + tab + r'''
#undef private
''' + function_source(ROOT/'src/menu.cpp', 'bool Menu::suppressesBinds() const') + '\n' + function_source(ROOT/'src/menu.cpp', 'void Menu::update(') + r'''
void*operator new(__SIZE_TYPE__,void*p){return p;}
alignas(8) static u8 storage[sizeof(LayoutProfilesTab)];static LayoutProfilesTab *page;
static Menu menu,*gMenu=&menu;static TMarioGamePad pad;
static bool grabbedBefore,suppressedBefore;
#define API extern "C" __declspec(dllexport)
API void reset(){LayoutProfiles::isBusy=LayoutProfiles::exists=false;
LayoutProfiles::currentGeneration=7;LayoutProfiles::savedGeneration=LayoutProfiles::saves=LayoutProfiles::loads=LayoutProfiles::refreshes=0;
JUTGamePad::mPadStatus[0].mButton=0;nav=0;toast=nullptr;
gBinds.mHeld=gBinds.mPrevHeld=0;gBinds.mRecSilent=false;for(u32 i=0;i<BIND_COUNT;++i)gBinds.mMask[i]=0;
pad.mButtons.mInput=pad.mButtons.mRapidInput=0;tabSwitches=pauseRequests=stepRequests=classicDispatches=0;
WarpWheel::sClassicInstantHeld=WarpWheel::sClassicInstantPending=WarpWheel::sClassicInstantSuppressed=false;
PatternSelector::inputAllowed=false;
page=new(storage)LayoutProfilesTab();page->focus();
menu.mShown=true;menu.mCurTab=menu.mToastFrames=menu.mCRepeatFrames=0;menu.mTabs[0]=page;
grabbedBefore=suppressedBefore=false;}
API void frame(u32 buttons){JUTGamePad::mPadStatus[0].mButton=(u16)buttons;
pad.mButtons.mInput=buttons;pad.mButtons.mRapidInput=buttons&~gBinds.mHeld;gBinds.sample();
grabbedBefore=page->grabsInput();suppressedBefore=menu.suppressesBinds();
if(suppressedBefore)gBinds.suppressUntilRelease();
const bool tasCinematic=false,stepOverridesShortcut=false,creationEditing=false,
sessionBlocksNewInput=false,wheelOwnsInputBeforeDirect=false,stateDiskBusy=false,sessionResultBeforeDirect=false;
''' + main_gate + r'''
if(WarpWheel::updateClassicInstant(&pad))++classicDispatches;
menu.update(&pad);}
API void press(u32 buttons){frame(buttons);frame(0);}
API void configure(u32 close,u32 pause){gBinds.mMask[BIND_MENU_TOGGLE]=(u16)close;gBinds.mMask[BIND_PRACTICE_PAUSE]=(u16)pause;}
API void down(){nav=TMarioGamePad::CSTICK_DOWN;frame(0);nav=0;}
API void existing(u32 yes,u32 gen){LayoutProfiles::exists=yes!=0;LayoutProfiles::currentGeneration=gen;}
API void busy(u32 yes){LayoutProfiles::isBusy=yes!=0;}
API void back(){page->back();}
API u32 get(u32 key){switch(key){case 0:return page->mMode;case 1:return page->mSel;
case 2:return LayoutProfiles::saves;case 3:return LayoutProfiles::savedGeneration;
case 4:return LayoutProfiles::savedSlot;case 5:return page->mLength;case 6:return LayoutProfiles::loads;
case 7:return menu.mShown;case 8:return page->mCursor;case 9:return pauseRequests;
case 10:return grabbedBefore;case 11:return suppressedBefore;case 12:return tabSwitches;case 13:return page->mPage;
case 14:return PatternSelector::inputAllowed;case 15:return WarpWheel::sClassicInstantSuppressed;case 16:return classicDispatches;}
return 0;}
'''
        path=Path(cls.temp.name)/'menu.cpp';path.write_text(fixture)
        proc=subprocess.run([str(ROOT/'toolchain/clang++.exe'),'--target=x86_64-pc-windows-msvc',
            '-shared','-nostdlib','-fuse-ld=lld','-Wl,/noentry','-O2','-fno-rtti','-fno-builtin',
            str(path),'-o',str(path.with_suffix('.dll'))],capture_output=True,text=True)
        if proc.returncode:raise RuntimeError(proc.stdout+proc.stderr)
        cls.lib=C.CDLL(str(path.with_suffix('.dll')))
        cls.addClassCleanup(lambda:C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def setUp(self):self.lib.reset()

    def test_y_saves_each_of_five_named_slots_from_list(self):
        for slot in range(5):
            self.lib.reset()
            for _ in range(slot):self.lib.down()
            self.lib.press(4);self.assertEqual(self.lib.get(0),2)
            self.lib.press(8)
            self.assertEqual([self.lib.get(i) for i in (0,2,3,4)],[0,1,0,slot])

    def test_replace_confirmation_pins_generation_and_cancel_keeps_data(self):
        self.lib.existing(1,9);self.lib.press(4);self.assertEqual(self.lib.get(0),1)
        self.lib.press(2);self.assertEqual([self.lib.get(i) for i in (0,2)],[0,0])
        self.lib.press(4);self.assertEqual(self.lib.get(0),1)
        self.lib.press(1);self.assertEqual(self.lib.get(0),2)
        self.lib.existing(1,10);self.lib.press(8)
        self.assertEqual([self.lib.get(i) for i in (2,3)],[1,9])

    def test_clear_name_requires_new_name_before_start_can_save(self):
        self.lib.press(4);self.assertEqual(self.lib.get(0),2)
        self.lib.press(16);self.assertEqual(self.lib.get(5),0)
        self.lib.press(8);self.assertEqual([self.lib.get(i) for i in (0,2)],[2,0])

    def test_dpad_down_bound_to_close_and_pause_moves_naming_cursor_only(self):
        self.lib.configure(1024,1024)
        self.lib.press(4);self.assertEqual(self.lib.get(0),2)
        self.lib.frame(1024)
        self.assertEqual([self.lib.get(i) for i in (0,7,8,9,10,11,14)], [2,1,8,0,1,1,0])
        self.lib.frame(1024)
        self.assertEqual([self.lib.get(i) for i in (7,8,9)], [1,8,0])
        self.lib.frame(0);self.lib.frame(1024)
        self.assertEqual([self.lib.get(i) for i in (0,7,8,9)], [2,1,16,0])

    def test_confirmation_and_final_save_frame_keep_input_from_close_and_pause(self):
        self.lib.existing(1,9);self.lib.press(4)
        self.assertEqual(self.lib.get(0),1)
        self.lib.configure(1,1);self.lib.frame(1)
        self.assertEqual([self.lib.get(i) for i in (0,7,9,10)], [2,1,0,1])
        self.lib.frame(0);self.lib.configure(8,8);self.lib.frame(8)
        self.assertEqual([self.lib.get(i) for i in (0,2,3,7,9,10,14)], [0,1,9,1,0,1,0])
        self.lib.frame(8)
        self.assertEqual([self.lib.get(i) for i in (2,7,9)], [1,1,0])
        self.lib.frame(0);self.lib.frame(8)
        self.assertEqual([self.lib.get(i) for i in (2,7,9,10,14)], [1,0,0,0,0])

    def test_explicit_cancel_frame_cannot_close_menu_or_pause_game(self):
        self.lib.press(4);self.lib.configure(32|8,32|8)
        self.lib.frame(32|8)
        self.assertEqual([self.lib.get(i) for i in (0,2,7,9,10,14)], [0,0,1,0,1,0])
        self.lib.frame(32|8)
        self.assertEqual([self.lib.get(i) for i in (2,7,9)], [0,1,0])
        self.lib.frame(0);self.lib.frame(32|8)
        self.assertEqual([self.lib.get(i) for i in (7,9)], [0,0])

    def test_configured_close_still_works_on_list_and_opening_frame_blocks_pause(self):
        self.lib.configure(1024,1024);self.lib.frame(1024)
        self.assertEqual([self.lib.get(i) for i in (0,7,9,10,14)], [0,0,0,0,0])
        self.lib.frame(1024);self.assertEqual(self.lib.get(7),0)
        self.lib.frame(0);self.lib.frame(1024)
        self.assertEqual([self.lib.get(i) for i in (0,7,9,14)], [0,1,0,0])

    def test_keyboard_page_chord_does_not_switch_tab_or_enable_pattern_selector(self):
        self.lib.press(4);self.lib.frame(256|1024)
        self.assertEqual([self.lib.get(i) for i in (0,7,12,13,14)], [2,1,0,1,0])

    def test_classic_restart_stays_suppressed_through_menu_close_until_release(self):
        restart=2|8192
        self.lib.configure(restart,0);self.lib.press(4)
        self.lib.frame(restart)
        self.assertEqual([self.lib.get(i) for i in (0,7,8,15,16)], [2,1,24,1,0])
        self.lib.frame(0);self.lib.press(32|8)
        self.lib.frame(restart)
        self.assertEqual([self.lib.get(i) for i in (7,15,16)], [0,1,0])
        self.lib.configure(0,0);self.lib.frame(restart)
        self.assertEqual([self.lib.get(i) for i in (7,15,16)], [0,1,0])
        self.lib.frame(0);self.assertEqual(self.lib.get(15),0)
        self.lib.frame(restart);self.assertEqual(self.lib.get(16),1)

    def test_storage_ownership_blocks_navigation_and_load_save_requests(self):
        self.lib.busy(1);self.lib.down();self.lib.press(4);self.lib.press(1)
        self.assertEqual([self.lib.get(i) for i in (0,1,2,6)],[0,0,0,0])


if __name__=='__main__':unittest.main()
