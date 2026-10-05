"""Run Mario colour INI and CARD migration/transaction code with host storage."""

import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_native_timer_creation import function

ROOT = Path(__file__).resolve().parents[1]


class Colors(C.Structure):
    _fields_ = [("magic", C.c_uint), ("version", C.c_ushort),
                ("enabled", C.c_ubyte), ("reserved0", C.c_ubyte),
                ("rgb", (C.c_ubyte * 3) * 7), ("reserved", C.c_ubyte * 3)]


class FluddColors(C.Structure):
    _fields_ = [("magic", C.c_uint), ("version", C.c_ushort), ("enabled", C.c_ushort),
                ("rgb", (C.c_ubyte * 3) * 10), ("reserved", C.c_ubyte * 26)]


class MarioColorPersistenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / "toolchain/clang++.exe"
        if not compiler.exists():
            raise unittest.SkipTest("Bundled Windows compiler required")
        cls.temp = tempfile.TemporaryDirectory(prefix="moonshine-mario-persistence-")
        cls.addClassCleanup(cls.temp.cleanup)
        work = Path(cls.temp.name)
        kernel = (ROOT / "launcher/kernel/SusamuneCfg.c").read_text()
        emulator = (ROOT / "src/emulator_persistence.cpp").read_text()
        keys = kernel[kernel.index("static const char *const MarioColorKeys"):]
        keys = keys[:keys.index("};") + 2]
        fludd_keys = kernel[kernel.index("static const char *const FluddColorKeys"):]
        keys += fludd_keys[:fludd_keys.index("};") + 2]
        records = emulator[emulator.index("constexpr u32 kRecordMagic"):emulator.index("// Only diskID")]
        state = emulator[emulator.index("struct State {"):]
        state = state[:state.index("};") + 2]
        offsets = emulator[emulator.index("constexpr u32 kProfilesOffsetV1"):emulator.index("bool validPBValue")]
        code = r'''
#define IS_EMULATOR 1
#include "susamune/susamune_cfg.h"
typedef unsigned char u8;typedef signed char s8;typedef unsigned short u16;
typedef unsigned int u32;typedef int s32;
#define API extern "C" __declspec(dllexport)
#define SUSAMUNE_GAME_VERSION 1
extern "C" void *memcpy(void*d,const void*s,__SIZE_TYPE__ n){u8*a=(u8*)d;const u8*b=(const u8*)s;while(n--)*a++=*b++;return d;}
extern "C" void *memset(void*d,int v,__SIZE_TYPE__ n){u8*a=(u8*)d;while(n--)*a++=(u8)v;return d;}
int strcmp(const char*a,const char*b){while(*a&&*a==*b){a++;b++;}return (u8)*a-(u8)*b;}
unsigned strlen(const char*s){unsigned n=0;while(s[n])n++;return n;}
char*strchr(char*s,int ch){while(*s&&*s!=ch)s++;return *s==ch?s:0;}
struct FIL {char text[2048];unsigned length;};
void Emit(FIL*f,int*,const char*s,u32 n){memcpy(f->text+f->length,s,n);f->length+=n;f->text[f->length]=0;}
char*number(char*out,unsigned value){char digits[10];unsigned n=0;do{digits[n++]=(char)('0'+value%10);value/=10;}while(value);while(n)*out++=digits[--n];return out;}
int _sprintf(char*out,const char*format,...){
 char*start=out;__builtin_va_list args;__builtin_va_start(args,format);
 while(*format){if(*format!='%'){*out++=*format++;continue;}format++;
  if(*format=='s'){const char*s=__builtin_va_arg(args,const char*);while(*s)*out++=*s++;}
  else if(*format=='u')out=number(out,__builtin_va_arg(args,unsigned));format++;}
 __builtin_va_end(args);*out=0;return (int)(out-start);
}
struct OSMutex {bool held;};struct DVDDiskID {u8 bytes[32];};
int mutexErrors,flushes;bool checkCommit,copyBeforeUnlock;u8*commitDestination;u8 expectedEnabled;
void OSInitMutex(OSMutex*m){m->held=false;}
void OSLockMutex(OSMutex*m){if(m->held)mutexErrors++;m->held=true;}
void OSUnlockMutex(OSMutex*m){if(!m->held)mutexErrors++;if(checkCommit)copyBeforeUnlock=commitDestination[6]==expectedEnabled;m->held=false;}
SusamuneMarioColorsCfg liveColors;SusamuneFluddColorsCfg liveFluddColors;SusamuneILEpisodesCfg liveEpisodes;SusamunePracticeDisplayStyleCfg livePracticeDisplays;
#undef SUSAMUNE_MARIO_COLORS_LIVE_PTR
#define SUSAMUNE_MARIO_COLORS_LIVE_PTR (&liveColors)
#undef SUSAMUNE_FLUDD_COLORS_LIVE_PTR
#define SUSAMUNE_FLUDD_COLORS_LIVE_PTR (&liveFluddColors)
#undef SUSAMUNE_IL_EPISODES_LIVE_PTR
#define SUSAMUNE_IL_EPISODES_LIVE_PTR (&liveEpisodes)
#undef SUSAMUNE_PRACTICE_DISPLAY_STYLE_LIVE_PTR
#define SUSAMUNE_PRACTICE_DISPLAY_STYLE_LIVE_PTR (&livePracticeDisplays)
static u8 liveSettingsTail[SUSAMUNE_CFG_SETTINGS_TAIL_SIZE];
#undef SUSAMUNE_CFG_SETTINGS_TAIL
#define SUSAMUNE_CFG_SETTINGS_TAIL(cfg) (liveSettingsTail)
void DCStoreRange(void*p,u32 n){if((p==&liveColors&&n==32)||(p==&liveFluddColors&&n==64)||(p==&liveEpisodes&&n==64)||(p==&livePracticeDisplays&&n==128)||(p==liveSettingsTail&&n==32))flushes++;else mutexErrors++;}
'''
        code += keys + records + state + offsets
        code += r'''
static State state;State*sState=&state;
enum InitResult {INIT_WAITING,INIT_READY,INIT_UNAVAILABLE};InitResult sInitResult=INIT_READY;
#undef SUSAMUNE_DOLPHIN_PERSIST_PPC_BASE
#define SUSAMUNE_DOLPHIN_PERSIST_PPC_BASE (&state)
enum {CARD_SLOTB=1,CARD_ERROR_READY=0,CARD_ERROR_NOFILE=-4,CARD_ERROR_IOERROR=-5};
struct CARDFileInfo {};
static Record card[2];bool fileExists,writeFails;
struct CardManager {OSMutex mMutex;u8 mCARDBlock[8192];void*mCardWorkArea;void unmount(){}} cardManager;
CardManager*gpCardManager=&cardManager;
int mount(void*){return CARD_ERROR_READY;}void unmount(){}
int CARDOpen(int,const char*,CARDFileInfo*){return fileExists?CARD_ERROR_READY:CARD_ERROR_NOFILE;}
int openOrCreate(CARDFileInfo*){fileExists=true;return CARD_ERROR_READY;}
int CARDRead(CARDFileInfo*,void*out,unsigned n,unsigned offset){memcpy(out,&card[offset/8192],n);return 0;}
int CARDWrite(CARDFileInfo*,void*in,unsigned n,unsigned offset){if(writeFails)return CARD_ERROR_IOERROR;memcpy(&card[offset/8192],in,n);return 0;}
int CARDClose(CARDFileInfo*){return 0;}
'''
        for name in ("initBlank", "migrateLegacyPBs", "validPBValue", "migrateProfilesV1",
                     "migrateRecordCfg", "initMarioColors", "publishMarioColors", "initFluddColors", "publishFluddColors", "initILEpisodes", "publishILEpisodes", "publishPracticeDisplays", "checksum",
                     "valid", "validV1", "validV2", "validV3", "validV4", "validV5", "validV6",
                     "validV7", "validV8", "validV9", "validV10", "validV11", "migrateRecordV6", "migrateRecordV7", "newer", "initState",
                     "writeRecordLocked", "loadRecords", "lock", "commit"):
            code += function(emulator, name)
        for name in ("ParseU16", "ParseQftU8", "ParseQftRgb", "InitMarioColorsDefaults",
                     "ApplyMarioColorsKey", "EmitMarioColors", "InitFluddColorsDefaults",
                     "ApplyFluddColorsKey", "EmitFluddColors"):
            code += function(kernel, name)
        code += r'''
API void defaults(SusamuneMarioColorsCfg*c){InitMarioColorsDefaults(c);}
API void fluddDefaults(SusamuneFluddColorsCfg*c){InitFluddColorsDefaults(c);}
API void fluddParse(SusamuneFluddColorsCfg*c,u16*mask,char*key,char*value){ApplyFluddColorsKey(c,key,value,mask);}
API const char*fluddEmit(SusamuneFluddColorsCfg*c){static FIL f;f.length=0;int err=0;EmitFluddColors(&f,&err,c);return f.text;}


API void parse(SusamuneMarioColorsCfg*c,u8*mask,const char*key,const char*value){ApplyMarioColorsKey(c,key,value,mask);}
API const char*emit(const SusamuneMarioColorsCfg*c){static FIL f;f.length=0;f.text[0]=0;int error=0;EmitMarioColors(&f,&error,c);return f.text;}
API int migrate(){
 static Record old;memset(&old,0xA5,sizeof(old));old.magic=kRecordMagic;old.version=7;
 old.payloadSize=kCfgSizeV7;old.gameVersion=1;old.cfg.magic=SUSAMUNE_CFG_MAGIC;
 old.cfg.version=SUSAMUNE_CFG_VERSION;old.cfg.flags=0x14800;old.checksum=checksum(&old);
 if(!validV7(&old)||valid(&old))return 1;
 static SusamuneCfg cfg;SusamuneMarioColorsCfg colors;
 migrateRecordV7(&cfg,&colors,&old.cfg);
 const u8*a=(const u8*)&cfg,*b=(const u8*)&old.cfg;
 for(unsigned i=0;i<sizeof(cfg);i++)
  if(i<__builtin_offsetof(SusamuneCfg,flags)||i>=__builtin_offsetof(SusamuneCfg,flags)+4)
   if(a[i]!=b[i])return 2;
 if(cfg.flags!=(old.cfg.flags|SUSAMUNE_CFG_FLAG_MARIO_COLORS|SUSAMUNE_CFG_FLAG_FLUDD_COLORS))return 3;
 SusamuneMarioColorsCfg expected;InitMarioColorsDefaults(&expected);
 a=(const u8*)&colors;b=(const u8*)&expected;for(unsigned i=0;i<32;i++)if(a[i]!=b[i])return 4;
 old.version=kRecordVersion;old.payloadSize=kRecordPayloadSize;old.cfg=cfg;old.marioColors=colors;
 old.marioColors.enabled=0x45;old.marioColors.rgb[6][2]=17;old.checksum=checksum(&old);
 if(!valid(&old)||validV7(&old))return 5;
 old.marioColors.rgb[6][2]^=1;if(valid(&old))return 6;
 return 0;
}
void makeRecord(Record*r,unsigned version,unsigned generation,unsigned enabled){
 memset(r,0,sizeof(*r));r->magic=kRecordMagic;r->version=version;
 r->payloadSize=version==7?kCfgSizeV7:version==8?kRecordPayloadSizeV8:version==9?kRecordPayloadSizeV9:version==10?kRecordPayloadSizeV10:version==11?kRecordPayloadSizeV11:kRecordPayloadSize;r->generation=generation;r->gameVersion=1;
 initBlank(&r->cfg);initMarioColors(&r->marioColors);initFluddColors(&r->fluddColors);initILEpisodes(&r->ilEpisodes);SusamunePracticeDisplayStyleInit(&r->practiceDisplays);r->marioColors.enabled=enabled;
 if(version<10)r->cfg.flags&=~SUSAMUNE_CFG_FLAG_IL_EPISODES;
 if(version<11)r->cfg.flags&=~SUSAMUNE_CFG_FLAG_PRACTICE_DISPLAY_STYLE;
 r->marioColors.rgb[4][1]=91;r->cfg.values[12]=37;r->cfg.nativeTimerStyle.scale=123;
 r->checksum=checksum(r);
}
API int cardRead(int test){
 mutexErrors=flushes=0;checkCommit=false;cardManager.mMutex.held=false;fileExists=true;
 initState();makeRecord(&card[0],kRecordVersion,10,3);makeRecord(&card[1],7,11,0xA5);
 if(test==1)makeRecord(&card[1],7,9,0xA5);
 if(test==2){makeRecord(&card[1],kRecordVersion,11,5);card[1].marioColors.rgb[6][1]^=1;}
 if(test==3){makeRecord(&card[1],kRecordVersion,11,5);card[1].gameVersion=2;card[1].checksum=checksum(&card[1]);}
 if(test==4){makeRecord(&card[1],8,11,5);memset(&card[1].fluddColors,0xa5,64);card[1].checksum=checksum(&card[1]);}
 static Record scratch;if(loadRecords(0,&scratch)!=0)return 1;publishMarioColors();publishFluddColors();
 if(mutexErrors||flushes!=7||state.cfg.values[12]!=37||state.cfg.nativeTimerStyle.scale!=123)return 2;
 if(!(state.cfg.flags&SUSAMUNE_CFG_FLAG_MARIO_COLORS))return 3;
 if(test==4){if(state.generation!=11||!state.initialSave||liveColors.enabled!=5||liveColors.rgb[4][1]!=91)return 6;
  if(liveFluddColors.enabled||liveFluddColors.rgb[9][2]!=255||liveFluddColors.magic!=SUSAMUNE_FLUDD_COLORS_MAGIC)return 7;
  return (state.cfg.flags&SUSAMUNE_CFG_FLAG_FLUDD_COLORS)?0:8;}
 if(test==0){if(state.generation!=11||!state.initialSave||liveColors.enabled||liveColors.rgb[4][1]!=255)return 4;}
 else if(state.generation!=10||state.initialSave||liveColors.enabled!=3||liveColors.rgb[4][1]!=91)return 5;
 return 0;
}
API int cardCommit(){
 mutexErrors=flushes=0;checkCommit=false;writeFails=false;fileExists=false;initState();
 if(!lock())return 1;liveColors.enabled=0x65;liveColors.rgb[6][2]=17;liveFluddColors.enabled=0x301;liveFluddColors.rgb[9][2]=83;
 expectedEnabled=0x65;commitDestination=(u8*)&state.marioColors;checkCommit=true;copyBeforeUnlock=false;
 const unsigned ticket=commit();checkCommit=false;
 if(ticket!=1||!copyBeforeUnlock||state.mutex.held||mutexErrors)return 2;
 if(state.fluddColors.enabled!=0x301||state.fluddColors.rgb[9][2]!=83)return 9;
 if(writeRecordLocked()!=0||!valid(&card[0])||card[0].marioColors.enabled!=0x65||card[0].marioColors.rgb[6][2]!=17)return 3;
 if(card[0].version!=12||card[0].payloadSize!=sizeof(SusamuneCfg)+320||card[0].fluddColors.enabled!=0x301||card[0].fluddColors.rgb[9][2]!=83)return 4;
 if(!lock())return 5;liveColors.enabled=2;commit();writeFails=true;
 if(writeRecordLocked()!=CARD_ERROR_IOERROR||state.activeRecord!=0||state.generation!=1||!valid(&card[0]))return 6;
 writeFails=false;if(writeRecordLocked()!=0||state.activeRecord!=1||!valid(&card[1])||card[1].marioColors.enabled!=2)return 7;
 return mutexErrors?8:0;
}
API int cardEpisodes(int legacy){
 mutexErrors=flushes=0;checkCommit=false;writeFails=false;fileExists=true;initState();
 makeRecord(&card[0],kRecordVersion,20,0x45);makeRecord(&card[1],legacy?9:kRecordVersion,21,0x65);
 card[1].fluddColors.enabled=0x301;card[1].fluddColors.rgb[9][2]=17;
 for(unsigned i=0;i<21;i++)card[1].ilEpisodes.episodes[i]=(i%8)+1;
 if(legacy)memset(&card[1].ilEpisodes,0xa5,64);
 card[1].checksum=checksum(&card[1]);
 const SusamuneMarioColorsCfg expectedMario=card[1].marioColors;
 const SusamuneFluddColorsCfg expectedFludd=card[1].fluddColors;
 static SusamuneCfg expectedCfg;expectedCfg=card[1].cfg;
 static Record scratch;if(loadRecords(0,&scratch)!=0)return 1;
 if(state.generation!=21||state.initialSave!=(bool)legacy)return 2;
 if(state.cfg.values[12]!=37||state.cfg.nativeTimerStyle.scale!=123)return 3;
 for(unsigned i=0;i<sizeof(SusamuneCfg);i++)
  if(i<__builtin_offsetof(SusamuneCfg,flags)||i>=__builtin_offsetof(SusamuneCfg,flags)+4)
   if(((u8*)&state.cfg)[i]!=((const u8*)&expectedCfg)[i])return 14;
 for(unsigned i=0;i<32;i++)if(((u8*)&state.marioColors)[i]!=((const u8*)&expectedMario)[i])return 4;
 for(unsigned i=0;i<64;i++)if(((u8*)&state.fluddColors)[i]!=((const u8*)&expectedFludd)[i])return 5;
 publishILEpisodes();
 if(liveEpisodes.magic!=SUSAMUNE_IL_EPISODE_MAGIC||liveEpisodes.count!=21)return 6;
 for(unsigned i=0;i<21;i++)if(liveEpisodes.episodes[i]!=(legacy?0:(i%8)+1))return 7;
 if(!(state.cfg.flags&SUSAMUNE_CFG_FLAG_IL_EPISODES))return 8;
 if(!lock())return 9;for(unsigned i=0;i<21;i++)liveEpisodes.episodes[i]=8-(i%8);commit();
 if(writeRecordLocked()!=0||!valid(&card[0])||card[0].version!=12)return 10;
 for(unsigned i=0;i<21;i++)if(card[0].ilEpisodes.episodes[i]!=8-(i%8))return 11;
 card[0].ilEpisodes.episodes[19]^=1;if(valid(&card[0]))return 12;
 return mutexErrors?13:0;
}
API int cardPracticeDisplays(unsigned version){
 mutexErrors=flushes=0;checkCommit=false;writeFails=false;fileExists=true;initState();
 makeRecord(&card[0],kRecordVersion,20,3);makeRecord(&card[1],version,21,5);
 SusamuneWallkickStyleCfg &old=card[1].cfg.wallkickStyle;
 old.magic=SUSAMUNE_WALLKICK_STYLE_MAGIC;old.version=SUSAMUNE_WALLKICK_STYLE_VERSION;
 old.x=77;old.y=144;old.scale=121;old.textA=199;
 for(unsigned i=0;i<sizeof(old.rgb);++i)((u8*)old.rgb)[i]=(u8)(40+i);
 static SusamunePracticeDisplayStyleCfg expected;
 if(version<11){memset(&card[1].practiceDisplays,0xa5,128);SusamunePracticeDisplayStyleFromWallkick(&expected,&old);}
 else{for(unsigned i=0;i<3;++i){auto &style=card[1].practiceDisplays.entries[i];
  style.x=80+i*60;style.y=180+i*30;style.scale=90+i*20;style.rgb[6][2]=61+i;}
  expected=card[1].practiceDisplays;}
 card[1].checksum=checksum(&card[1]);
 if(sizeof(Record)!=8192||__builtin_offsetof(Record,practiceDisplays)!=32+kRecordPayloadSizeV10)return 1;
 if((version==10&&!validV10(&card[1]))||(version==11&&!validV11(&card[1]))||(version==12&&!valid(&card[1])))return 2;
 static Record scratch;if(loadRecords(0,&scratch)!=0)return 3;publishPracticeDisplays();
 if(state.generation!=21||state.initialSave!=(version<12)||mutexErrors)return 4;
 for(unsigned i=0;i<128;++i)if(((u8*)&livePracticeDisplays)[i]!=((u8*)&expected)[i])return 5;
 if(!(state.cfg.flags&SUSAMUNE_CFG_FLAG_PRACTICE_DISPLAY_STYLE)||state.cfg.values[12]!=37)return 6;
 if(version>=10&&state.ilEpisodes.magic!=SUSAMUNE_IL_EPISODE_MAGIC)return 7;
 if(!lock())return 8;livePracticeDisplays.entries[1].textA=91;commit();expected.entries[1].textA=91;
 for(unsigned i=0;i<128;++i)if(((u8*)&state.practiceDisplays)[i]!=((u8*)&expected)[i])return 9;
 if(writeRecordLocked()!=0||!valid(&card[0])||card[0].version!=12||card[0].generation!=22)return 10;
 initState();if(loadRecords(0,&scratch)!=0)return 11;publishPracticeDisplays();
 if(state.initialSave||state.generation!=22)return 12;
 for(unsigned i=0;i<128;++i)if(((u8*)&livePracticeDisplays)[i]!=((u8*)&expected)[i])return 13;
 card[0].practiceDisplays.entries[2].rgb[6][2]^=1;
 if(valid(&card[0]))return 14;
 initState();if(loadRecords(0,&scratch)!=0||state.generation!=21)return 15;
 return mutexErrors?16:0;
}
'''
        source = work / "persistence.cpp"
        source.write_text(code, encoding="ascii")
        dll = work / "persistence.dll"
        subprocess.run([str(compiler), "--target=x86_64-pc-windows-msvc", "-shared",
                        "-nostdlib", "-fno-builtin", "-fuse-ld=lld", "-Wl,/noentry",
                        "-I", str(ROOT / "include"), str(source), "-o", str(dll)], check=True)
        cls.lib = C.CDLL(str(dll))
        cls.addClassCleanup(lambda: C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))
        cls.lib.defaults.argtypes = [C.POINTER(Colors)]
        cls.lib.parse.argtypes = [C.POINTER(Colors), C.POINTER(C.c_ubyte), C.c_char_p, C.c_char_p]
        cls.lib.emit.argtypes = [C.POINTER(Colors)]
        cls.lib.emit.restype = C.c_char_p
        cls.lib.fluddDefaults.argtypes = [C.POINTER(FluddColors)]
        cls.lib.fluddParse.argtypes = [C.POINTER(FluddColors), C.POINTER(C.c_ushort), C.c_char_p, C.c_char_p]
        cls.lib.fluddEmit.argtypes = [C.POINTER(FluddColors)]
        cls.lib.fluddEmit.restype = C.c_char_p

    def parse(self, entries):
        colors, explicit = Colors(), C.c_ubyte(255)
        self.lib.defaults(C.byref(colors))
        for key, value in entries:
            self.lib.parse(C.byref(colors), C.byref(explicit), key.encode(), value.encode())
        return colors

    def test_mask_wins_before_or_after_rgb_and_rgb_only_enables_its_part(self):
        rgb = [("mario_cap_rgb", "0,1,255"), ("mario_sunshine_shirt_rgb", "19, 23, 47")]
        for mask in (0, 2, 127):
            before = self.parse([("mario_colors_enabled", str(mask)), *rgb])
            after = self.parse([*rgb, ("mario_colors_enabled", str(mask))])
            self.assertEqual(bytes(before), bytes(after))
            self.assertEqual(before.enabled, mask)
            self.assertEqual(list(before.rgb[6]), [19, 23, 47])
        self.assertEqual(self.parse(rgb).enabled, 65)

    def test_invalid_mask_rgb_and_unknown_key_do_not_change_data(self):
        base = [("mario_cap_rgb", "20,30,40"), ("mario_colors_enabled", "1")]
        expected = bytes(self.parse(base))
        for key, value in (("mario_colors_enabled", "128"), ("mario_colors_enabled", "-1"),
                           ("mario_colors_enabled", "9999999999999"), ("mario_cap_rgb", "256,0,0"),
                           ("mario_cap_rgb", "1,2"), ("mario_cap_rgb", "1,2,3,4"),
                           ("mario_hat_rgb", "1,2,3")):
            with self.subTest(key=key, value=value):
                self.assertEqual(bytes(self.parse([*base, (key, value)])), expected)

    def test_emitted_all_colours_and_disabled_mask_roundtrip(self):
        colors = self.parse([("mario_shoes_rgb", "12,34,56"), ("mario_colors_enabled", "0")])
        emitted = self.lib.emit(C.byref(colors)).decode()
        entries = [tuple(part.strip() for part in line.split("=", 1)) for line in emitted.splitlines()]
        self.assertEqual(len(entries), 8)
        self.assertEqual(entries[-1], ("mario_colors_enabled", "0"))
        self.assertEqual(bytes(self.parse(entries)), bytes(colors))

    def parse_fludd(self, entries):
        colors, explicit = FluddColors(), C.c_ushort(65535)
        self.lib.fluddDefaults(C.byref(colors))
        for key, value in entries:
            self.lib.fluddParse(C.byref(colors), C.byref(explicit), key.encode(), value.encode())
        return colors

    def test_fludd_high_mask_bits_and_all_ten_parts_roundtrip(self):
        keys = ("paint", "metal", "straps", "model_tank", "spray_nozzle", "hover_nozzle",
                "rocket_nozzle", "turbo_nozzle", "water", "water_highlight")
        rgb = [(f"fludd_{key}_rgb", f"{i},2,255") for i, key in enumerate(keys)]
        for mask in (0, 256, 512, 1023):
            before = self.parse_fludd([("fludd_colors_enabled", str(mask)), *rgb])
            after = self.parse_fludd([*rgb, ("fludd_colors_enabled", str(mask))])
            self.assertEqual(bytes(before), bytes(after))
            self.assertEqual(before.enabled, mask)
            self.assertEqual(list(before.rgb[9]), [9, 2, 255])
            emitted = self.lib.fluddEmit(C.byref(before)).decode()
            entries = [tuple(p.strip() for p in line.split("=", 1)) for line in emitted.splitlines()]
            self.assertEqual(len(entries), 11)
            self.assertEqual(bytes(self.parse_fludd(entries)), bytes(before))
        self.assertEqual(self.parse_fludd(rgb).enabled, 1023)

    def test_fludd_rejects_bad_masks_and_keeps_existing_native_hud_key_separate(self):
        base = [("fludd_water_rgb", "14,23,58"), ("fludd_colors_enabled", "256")]
        expected = bytes(self.parse_fludd(base))
        for key, value in (("fludd_colors_enabled", "1024"), ("fludd_colors_enabled", "-1"),
                           ("fludd_colors_enabled", "9999999999999"), ("fludd_water_rgb", "256,0,0"),
                           ("fludd_tank_rgb", "1,2,3"), ("mario_cap_rgb", "1,2,3")):
            with self.subTest(key=key, value=value):
                self.assertEqual(bytes(self.parse_fludd([*base, (key, value)])), expected)

    def test_v7_preserves_every_old_byte_and_v8_checksums_colour_tail(self):
        self.assertEqual(self.lib.migrate(), 0)

    def test_card_generation_selection_and_old_padding_never_becomes_colours(self):
        for case in range(5):
            with self.subTest(case=case):
                self.assertEqual(self.lib.cardRead(case), 0)

    def test_commit_copies_live_colours_under_lock_and_failed_write_keeps_old_record(self):
        self.assertEqual(self.lib.cardCommit(), 0)

    def test_card_v9_migration_and_v10_keep_colours_and_episode_choices(self):
        for legacy in (0, 1):
            with self.subTest(legacy=legacy):
                self.assertEqual(self.lib.cardEpisodes(legacy), 0)

    def test_three_practice_styles_roundtrip_and_legacy_records_ignore_padding(self):
        for version in (7, 9, 10, 11, 12):
            with self.subTest(version=version):
                self.assertEqual(self.lib.cardPracticeDisplays(version), 0)


if __name__ == "__main__":
    unittest.main()
