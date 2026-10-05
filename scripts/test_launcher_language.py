"""Run the loader's real staging path with app-selected language and fake FAT."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_native_timer_creation import function

ROOT = Path(__file__).resolve().parents[1]


class LauncherLanguageStagingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(prefix='moonshine-language-stage-')
        cls.addClassCleanup(cls.work.cleanup)
        source = (ROOT/'launcher/loader/source/SusamuneMod.c').read_text()
        harness = r'''
typedef __SIZE_TYPE__ size_t;typedef unsigned int u32,UINT;typedef unsigned char u8;typedef int bool;
typedef unsigned long long FSIZE_t;
#define NULL ((void*)0)
#define FR_OK 0
#define FA_READ 1
#define FA_OPEN_EXISTING 0
#define MAXPATHLEN 1024
#define SUSAMUNE_MOD_HEADER_SIZE 32
#define SUSAMUNE_MOD_STAGED_FILE_MAX_SIZE 0x9F000
#define SUSAMUNE_JP_UI_OFFSET 0x86000
#define SUSAMUNE_JP_UI_SIZE 0x19000
#define SUSAMUNE_JP_UI_FILENAME "ja_ui.bin"
#define SUSAMUNE_MOD_FILE_FMT "mod_%s.bin"
static u8 staging[0x9F000];
#define SUSAMUNE_MOD_PPC_PTR ((struct SusamuneModHeader*)staging)
#define SUSAMUNE_JP_UI_PPC_BASE (staging+SUSAMUNE_JP_UI_OFFSET)
#define SUSAMUNE_MOD_REGION_TAG(id) ((id)==0x474D534A?"jp":(id)==0x474D5345?"us":(id)==0x474D5350?"pal":NULL)
struct SusamuneModHeader {u32 magic,words[7];};
typedef struct {struct{FSIZE_t objsize;}obj;int asset;} FIL;
char launch_dir[MAXPATHLEN]="sd:/apps/selected/";
static unsigned requested,assetOpens,modOpens,assetFlushes,modFlushes,errors;
static FSIZE_t modSize;
void *memset(void*p,int v,size_t n){u8*d=p;while(n--)*d++=v;return p;}
int snprintf(char*dst,size_t n,const char*format,const char*directory,...){size_t i=0;while(directory[i]&&i+1<n){dst[i]=directory[i];i++;}const char*tail=format[2]=='j'?"ja_ui.bin":"mod_jp.bin";while(*tail&&i+1<n)dst[i++]=*tail++;dst[i]=0;return (int)i;}
void gprintf(const char*p,...){(void)p;}
int SusamuneTextJapaneseRequested(void){return requested;}
int SusamuneModFileValid(const struct SusamuneModHeader*h,u32 id,u32 n){return !(errors&2)&&h->magic==0x534D4F44&&n==modSize;}
int SusamuneJpUiValid(const u8*p,unsigned n){return !(errors&16)&&n==64&&p[0]=='M'&&p[1]=='J';}
int f_open_char(FIL*f,const char*p,unsigned mode){unsigned i=0;while(p[i])i++;f->asset=i>=9&&p[i-9]=='j';if(f->asset){assetOpens++;f->obj.objsize=64;return (errors&4)?1:0;}modOpens++;f->obj.objsize=modSize;return (errors&1)?1:0;}
int f_read(FIL*f,void*p,unsigned n,UINT*out){memset(p,0,n);*out=n;if(f->asset){((u8*)p)[0]='M';((u8*)p)[1]='J';return (errors&8)?1:0;}((struct SusamuneModHeader*)p)->magic=0x534D4F44;return 0;}
int f_close(FIL*f){return 0;}
void DCFlushRange(void*p,unsigned n){if(p==SUSAMUNE_JP_UI_PPC_BASE)assetFlushes++;else modFlushes++;}
'''
        for name in ('ValidModFile','LoadJapaneseUi','SusamuneLoadMod'):
            harness += function(source,name)
        harness += r'''
__declspec(dllexport) void run(unsigned language,unsigned game,unsigned fail,unsigned size){
 requested=language;errors=fail;modSize=size;assetOpens=modOpens=assetFlushes=modFlushes=0;
 memset(SUSAMUNE_JP_UI_PPC_BASE,0xA5,64);SusamuneLoadMod(game);}
__declspec(dllexport) unsigned result(unsigned which){switch(which){case 0:return assetOpens;case 1:return modOpens;case 2:return assetFlushes;case 3:return modFlushes;case 4:return SUSAMUNE_MOD_PPC_PTR->magic;case 5:return SUSAMUNE_JP_UI_PPC_BASE[0];}return 0;}
__declspec(dllexport) unsigned cleared(void){for(unsigned i=0;i<64;i++)if(SUSAMUNE_JP_UI_PPC_BASE[i])return 0;return 1;}
'''
        path=Path(cls.work.name)/'staging.c';path.write_text(harness)
        dll=path.with_suffix('.dll')
        result=subprocess.run([str(ROOT/'toolchain/clang.exe'),'--target=x86_64-pc-windows-msvc',
            '-shared','-nostdlib','-fno-builtin','-O2','-fuse-ld=lld','-Wl,/noentry',str(path),'-o',str(dll)],capture_output=True,text=True)
        if result.returncode:raise AssertionError(result.stderr)
        cls.lib=C.CDLL(str(dll))
        cls.addClassCleanup(lambda:C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def test_english_clears_leftover_japanese_asset_for_every_game_region(self):
        for game in (0x474D534A,0x474D5345,0x474D5350):
            self.lib.run(0,game,0,96)
            self.assertEqual(self.lib.result(0),0)
            self.assertEqual(self.lib.cleared(),1)
            self.assertEqual(self.lib.result(4),0x534D4F44)
            self.assertEqual(self.lib.result(2),1)

    def test_japanese_asset_requires_requested_language_and_compatible_mod(self):
        self.lib.run(1,0x474D534A,0,96)
        self.assertEqual(self.lib.result(0),1)
        self.assertEqual(self.lib.result(5),ord('M'))
        self.assertEqual(self.lib.result(2),2)
        for game,errors,size in ((0x474D5345,0,96),(0x474D5350,0,96),(0,0,96),
                                 (0x474D534A,1,96),(0x474D534A,2,96),(0x474D534A,0,0x86004)):
            self.lib.run(1,game,errors,size)
            self.assertEqual(self.lib.result(0),0)
            self.assertEqual(self.lib.cleared(),1)

    def test_failed_or_corrupt_asset_cannot_leave_a_japanese_ready_header(self):
        for error in (4,8,16):
            self.lib.run(1,0x474D534A,error,96)
            self.assertEqual(self.lib.result(0),1)
            self.assertEqual(self.lib.cleared(),1)
            self.assertEqual(self.lib.result(4),0x534D4F44)


if __name__=='__main__':unittest.main()
