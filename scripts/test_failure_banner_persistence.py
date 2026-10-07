"""Failure Creation style survives the production regional INI and profile paths."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

import test_iling_episode_persistence as episodes
from test_native_timer_creation import function

ROOT=Path(__file__).resolve().parents[1]


class FailureStyle(C.Structure):
    _fields_=[('magic',C.c_uint),('x',C.c_ushort),('y',C.c_ushort),
              ('scale',C.c_ubyte),('textA',C.c_ubyte),('bgR',C.c_ubyte),
              ('bgG',C.c_ubyte),('bgB',C.c_ubyte),('bgA',C.c_ubyte),
              ('textBrightness',C.c_ubyte),('padding',C.c_ubyte),
              ('rgb',C.c_ubyte*3),('reserved',C.c_ubyte)]


class FailureBannerPersistenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        episodes.EpisodePersistenceTests.setUpClass.__func__(cls)
        cls.lib.failure.argtypes=[C.POINTER(FailureStyle)]
        cls.lib.layoutFailure.argtypes=[C.c_uint,C.POINTER(FailureStyle)]

    def style(self):
        out=FailureStyle();self.lib.failure(C.byref(out));return out

    def test_all_creation_fields_roundtrip_without_touching_other_regions(self):
        fields={b'x':613,b'y':401,b'scale':157,b'text_alpha':177,
                b'background_rgb':b'5,19,222',b'background_alpha':123,
                b'text_brightness':81,b'padding':17,b'1_rgb':b'32,200,70'}
        for region in (b'jp',b'us',b'pal'):
            self.lib.selectRegion(region)
            other=b'[creation_other]\r\nstreak_failure_x = 29\r\n'
            text=other+b'[creation_'+region+b']\r\n'+b''.join(
                b'streak_failure_'+key+b' = '+(value if isinstance(value,bytes) else str(value).encode())+b'\r\n'
                for key,value in fields.items())
            self.lib.parse(text);first=self.style()
            self.assertEqual(C.sizeof(first),20)
            self.assertEqual((first.magic,first.x,first.y,first.scale),(0x4d464231,613,401,157))
            self.assertEqual((first.textA,first.bgR,first.bgG,first.bgB,first.bgA,first.textBrightness,first.padding),
                             (177,5,19,222,123,81,17))
            self.assertEqual(list(first.rgb),[32,200,70])
            written=self.lib.rewrite(text)
            self.assertIn(other,written)
            self.assertNotIn(b'streak_failure_2_rgb',written)
            self.lib.selectRegion(region);self.lib.parse(written)
            self.assertEqual(bytes(self.style()),bytes(first))

    def test_missing_style_leaves_magic_absent_for_legacy_numeric_fallback(self):
        self.lib.selectRegion(b'pal')
        text=b'[settings_pal]\r\nstreak_failure_x = 17\r\nstreak_failure_y = 12\r\nstreak_failure_size = 7\r\n'
        self.lib.parse(text)
        self.assertEqual(self.style().magic,0)
        written=self.lib.rewrite(text)
        self.assertEqual(written.count(b'streak_failure_x = 17'),1)
        self.assertNotIn(b'streak_failure_1_rgb',written)

    def test_new_profile_preserves_reserved_style_slices_and_old_profile_migrates(self):
        self.lib.selectRegion(b'us')
        self.lib.parse(b'[creation_us]\r\nstreak_failure_x = 501\r\nstreak_failure_1_rgb = 1,23,245\r\n')
        original=self.style();loaded=FailureStyle()
        self.assertEqual(self.lib.layoutFailure(0,C.byref(loaded)),1)
        self.assertEqual(bytes(loaded),bytes(original))
        self.lib.selectRegion(b'us')
        self.assertEqual(self.lib.layoutFailure(1,C.byref(loaded)),1)
        self.assertEqual(loaded.magic,0)

    def test_bad_numbers_and_unrelated_creation_keys_cannot_change_confirmed_style(self):
        self.lib.selectRegion(b'jp')
        self.lib.parse(b'[creation_jp]\r\nstreak_failure_x = bad\r\nstreak_failure_unknown = 9\r\n')
        self.assertEqual(self.style().magic,0)
        self.lib.parse(b'[creation_jp]\r\nstreak_failure_x = 210\r\nstreak_failure_y = 300\r\n')
        before=bytes(self.style())
        self.lib.parse(b'[creation_jp]\r\nstreak_failure_x = -2\r\nstreak_failure_y = 65536\r\n'
                       b'streak_failure_scale = 999\r\nstreak_failure_1_rgb = 300,2,3\r\n'
                       b'jump_timing_x = 19\r\nwallkick_x = 42\r\n')
        self.assertEqual(bytes(self.style()),before)


class FailureBannerAdoptionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='moonshine-failure-adoption-')
        cls.addClassCleanup(cls.temp.cleanup)
        source=(ROOT/'src/creation_extras.cpp').read_text()
        code=r'''
#define private public
#include "susamune/creation_extras.hxx"
#undef private
extern "C" void *memcpy(void*d,const void*s,__SIZE_TYPE__ n){auto*a=(u8*)d;auto*b=(const u8*)s;while(n--)*a++=*b++;return d;}
extern "C" void *memset(void*d,int v,__SIZE_TYPE__ n){auto*a=(u8*)d;while(n--)*a++=(u8)v;return d;}
extern "C" int memcmp(const void*a,const void*b,__SIZE_TYPE__ n){auto*x=(const u8*)a;auto*y=(const u8*)b;while(n--){if(*x!=*y)return*x-*y;++x;++y;}return 0;}
enum{SETTING_STREAK_FAILURE_X,SETTING_STREAK_FAILURE_Y,SETTING_STREAK_FAILURE_SIZE};
struct Settings{int get(int i){return i+2;}}gSettings;
'''
        for name in ('clampi','clampStyle','loadStyle','storeStyle',
                     'CreationExtras::defaultPracticeStyle',
                     'CreationExtras::adoptWallkick','CreationExtras::adoptPracticeDisplays'):
            code+=function(source,name)
        code+=r'''
CreationExtras extras;
#define API extern "C" __declspec(dllexport)
API void adopt(unsigned mode,MoonshineFailureStyle*out){
 SusamuneWallkickStyleCfg wall={};wall.magic=SUSAMUNE_WALLKICK_STYLE_MAGIC;wall.version=SUSAMUNE_WALLKICK_STYLE_VERSION;
 SusamunePracticeDisplayStyleCfg practice;SusamunePracticeDisplayStyleInit(&practice);
 MoonshineFailureStyle style;MoonshineFailureStyleInit(&style);style.x=500;style.y=400;style.rgb[0][0]=73;
 if(mode==5){style.x=65535;style.y=65535;style.scale=255;style.textBrightness=0;style.padding=254;}
 MoonshineFailureStyleWrite(&style,&wall,&practice);
 MoonshineFailureStyleInit(&extras.mFailureBanner);extras.mFailureBanner.x=621;
 if(mode==1)wall.magic=0;
 if(mode==2)practice.magic=0;
 if(mode==3)memset(practice.reserved,0,sizeof(practice.reserved));
 if(mode==4){memset(wall.reserved1,0,sizeof(wall.reserved1));memset(practice.reserved,0,sizeof(practice.reserved));}
 extras.adoptWallkick(&wall);extras.adoptPracticeDisplays(&practice);*out=extras.mFailureBanner;
}
'''
        work=Path(cls.temp.name);(work/'Dolphin').mkdir()
        (work/'Dolphin/types.h').write_text('typedef unsigned char u8;typedef unsigned short u16;typedef unsigned u32;')
        path=work/'adoption.cpp';path.write_text(code)
        proc=subprocess.run([str(ROOT/'toolchain/clang++.exe'),'--target=x86_64-pc-windows-msvc','-shared','-nostdlib',
                             '-fno-builtin','-fuse-ld=lld','-Wl,/noentry','-O2','-I',str(work),'-I',str(ROOT/'include'),
                             str(path),'-o',str(path.with_suffix('.dll'))],capture_output=True,text=True)
        if proc.returncode:raise RuntimeError(proc.stderr)
        cls.lib=C.CDLL(str(path.with_suffix('.dll')))
        cls.lib.adopt.argtypes=[C.c_uint,C.POINTER(FailureStyle)]
        cls.addClassCleanup(lambda:C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def test_both_valid_slices_adopt_exactly_and_invalid_halves_use_legacy_fallback(self):
        result=FailureStyle();self.lib.adopt(0,C.byref(result))
        self.assertEqual((result.x,result.y,result.rgb[0]),(500,400,73))
        for mode in (1,2,3,4):
            self.lib.adopt(mode,C.byref(result))
            self.assertEqual((result.magic,result.x,result.y,result.scale,result.textA,result.reserved),
                             (0x4d464231,52,67,90,255,0xa5),mode)

    def test_authored_bounds_are_clamped_before_display(self):
        result=FailureStyle();self.lib.adopt(5,C.byref(result))
        self.assertEqual((result.x,result.y,result.scale,result.textBrightness,result.padding),(640,456,200,25,16))


if __name__=='__main__':unittest.main()
