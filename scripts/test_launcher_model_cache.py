"""Run the loader's model-cache transaction and staging fallback with faulted FAT I/O."""
import ctypes as C
import _ctypes
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib

from test_practice_tape import function_source
import test_shadow_asset_checksums as shadow_checks

ROOT = Path(__file__).resolve().parents[1]

FIXTURE = r'''
typedef unsigned char u8;typedef unsigned short u16;typedef unsigned u32;
typedef unsigned long long u64;typedef int s32;typedef unsigned uLong;
typedef unsigned char Bytef;typedef __SIZE_TYPE__ size_t;
typedef int FRESULT;typedef unsigned UINT;
#define NULL 0
#define Z_NULL NULL
#define FR_OK 0
#define FR_NO_FILE 4
#define FR_NO_PATH 5
#define FR_EXIST 8
#define FA_READ 1
#define FA_WRITE 2
#define FA_OPEN_EXISTING 0
#define FA_CREATE_ALWAYS 8
#define AM_DIR 16
#define SHADOW_INPUT_SIZE 0x8000u
#define SHADOW_COMPRESSED_MAX_SIZE 0x600000u
#define SUSAMUNE_CISO_MAP_COUNT 1024
#define MOONSHINE_DATA_ROOT "/Moonshine data"
#define SUSAMUNE_GHOST_SHADOW_STATUS_SOURCE_UNSUPPORTED -1
#define SUSAMUNE_GHOST_SHADOW_STATUS_OPEN_FAILED -2
#define SUSAMUNE_GHOST_SHADOW_STATUS_READ_FAILED -3
#define SUSAMUNE_GHOST_SHADOW_STATUS_BAD_YAZ0 -4
#define SUSAMUNE_GHOST_SHADOW_STATUS_RESOURCE_MISSING -5
#define SUSAMUNE_GHOST_SHADOW_STATUS_BAD_CHECKSUM -6
#define SUSAMUNE_GHOST_SHADOW_STATUS_READY 1
#define SHADOW_SOURCE_FILE 0
#define SHADOW_SOURCE_REAL_DISC 1
#define NIN_CFG_CFG_ON_USB 1
#define gprintf(...) ((void)0)
struct Config{unsigned Config;};static Config config;static Config*ncfg=&config;
extern "C" void *memcpy(void*d,const void*s,size_t n){u8*a=(u8*)d;const u8*b=(const u8*)s;while(n--)*a++=*b++;return d;}
extern "C" void *memset(void*d,int v,size_t n){u8*a=(u8*)d;while(n--)*a++=(u8)v;return d;}
extern "C" int memcmp(const void*a,const void*b,size_t n){const u8*x=(const u8*)a,*y=(const u8*)b;while(n--){if(*x!=*y)return *x-*y;++x;++y;}return 0;}
static unsigned strlen(const char*p){unsigned n=0;while(p[n])++n;return n;}
static bool equal(const char*a,const char*b){return strlen(a)==strlen(b)&&!memcmp(a,b,strlen(a));}
static bool suffix(const char*p,const char*s){unsigned n=strlen(p),k=strlen(s);return n>=k&&equal(p+n-k,s);}
static void text(char*d,const char*s){memcpy(d,s,strlen(s)+1);}
static void hex(char*&d,unsigned long value){for(int n=7;n>=0;--n)*d++="0123456789abcdef"[(value>>(n*4))&15];}
static int snprintf(char*out,size_t cap,const char*fmt,...){
 char b[256],*p=b;__builtin_va_list args;__builtin_va_start(args,fmt);
 while(*fmt){if(fmt[0]=='%'&&fmt[1]=='s'){const char*s=__builtin_va_arg(args,const char*);while(*s)*p++=*s++;fmt+=2;}
 else if(fmt[0]=='%'&&!memcmp(fmt,"%08lx",5)){hex(p,__builtin_va_arg(args,unsigned long));fmt+=5;}
 else *p++=*fmt++;}__builtin_va_end(args);unsigned n=(unsigned)(p-b);*p=0;
 if(cap){unsigned k=n<cap-1?n:(unsigned)cap-1;memcpy(out,b,k);out[k]=0;}return n;
}
struct FIL{struct{u64 objsize;}obj;unsigned id,position,mode;};
struct FILINFO{unsigned fattrib;};
struct DiskFile{char path[192];u8 bytes[0x13000];unsigned size;bool exists;};
static DiskFile files[10];static unsigned faults,reads,readBytes,writes,writeBytes,decodes,finds,sourceReads,sourceBytes,published,closed,flushes;
static bool missingSource,failDecode;static int finalStatus;static char sourceDevice[4];
static u8 original[0x13000],payload[0x13000+32],identity[32],sDiscReadScratch[SHADOW_INPUT_SIZE+32];
static unsigned originalSize;static unsigned short sCisoMap[1024];
static uLong (*crc32)(uLong,const Bytef*,u32);
static int findFile(const char*p){for(unsigned i=0;i<10;++i)if(files[i].exists&&equal(files[i].path,p))return (int)i;return -1;}
static int f_open_char(FIL*f,const char*p,unsigned mode){int i=findFile(p);
 if(mode&FA_WRITE){if(faults&64)return 1;if(i<0)for(unsigned j=0;j<10;++j)if(!files[j].exists){i=(int)j;break;}
 if(i<0)return 1;files[i].exists=true;files[i].size=0;text(files[i].path,p);}
 if(i<0)return FR_NO_FILE;f->id=(unsigned)i;f->position=0;f->mode=mode;f->obj.objsize=files[i].size;return FR_OK;}
static int f_read(FIL*f,void*d,unsigned n,UINT*got){++reads;*got=0;
 if((faults&1)||((faults&16)&&suffix(files[f->id].path,".tmp")))return 1;
 unsigned left=files[f->id].size-f->position;if(n>left)n=left;
 memcpy(d,files[f->id].bytes+f->position,n);f->position+=n;*got=n;readBytes+=n;return FR_OK;}
static int f_write(FIL*f,const void*s,unsigned n,UINT*got){++writes;*got=0;if(n>sizeof(files[0].bytes)-f->position)return 1;
 if(faults&2){if(n)--n;}memcpy(files[f->id].bytes+f->position,s,n);f->position+=n;files[f->id].size=f->position;*got=n;writeBytes+=n;return FR_OK;}
static int f_sync(FIL*){return faults&4?1:0;}
static int f_close(FIL*f){++closed;return ((faults&8)&&(f->mode&FA_WRITE))||((faults&256)&&!(f->mode&FA_WRITE))?1:0;}
static int f_stat_char(const char*p,FILINFO*i){if(faults&128)return 1;if(findFile(p)<0)return FR_NO_FILE;i->fattrib=0;return FR_OK;}
static int f_mkdir_char(const char*){return faults&512?1:FR_EXIST;}
static int f_unlink_char(const char*p){int i=findFile(p);if(i<0)return FR_NO_FILE;files[i].exists=false;return FR_OK;}
static int f_rename_char(const char*a,const char*b){if(faults&32)return 1;int i=findFile(a);if(i<0)return FR_NO_FILE;
 if(findFile(b)>=0)return FR_EXIST;text(files[i].path,b);return FR_OK;}
struct ShadowReader{unsigned kind;FIL file;bool fileOpen;unsigned discCommand;u64 size;bool ciso;u32 cacheSize[2];};
struct ShadowDecoder{u8 data[64];};static ShadowDecoder decoder;
static ShadowDecoder*memalign(unsigned,size_t){return &decoder;}static void free(void*){}
struct mallinfo{unsigned uordblks,fordblks;};static struct mallinfo mallinfo(){return {0,0};}
static void DCFlushRange(void*,unsigned){++flushes;}
'''


class LauncherModelCacheTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / 'toolchain/clang++.exe'
        if not compiler.exists():
            raise unittest.SkipTest('Bundled Windows compiler required')
        cls.temp = tempfile.TemporaryDirectory(prefix='moonshine-model-cache-')
        cls.addClassCleanup(cls.temp.cleanup)
        path = ROOT / 'launcher/loader/source/SusamuneShadowAsset.c'
        production = path.read_text()
        spec = production[production.index('typedef struct AssetSpec {'):
                          production.index('} AssetSpec;') + len('} AssetSpec;')]
        helpers = production[production.index('static int ValidatePayload('):
                             production.index('static void StageAsset(')]
        code = FIXTURE + spec
        for name in ('static u16 ReadBE16(', 'static u32 ReadBE32('):
            code += function_source(path, name)
        code += helpers + r'''
static void PublishStatus(const AssetSpec*,int status){finalStatus=status;}
static void PublishReady(const AssetSpec*){finalStatus=1;++published;}
static bool IsSupportedFileExt(const char*){return true;}
static bool OpenGameFile(ShadowReader*r,const char*device,const char*){text(sourceDevice,device);r->size=0x57058000;return !missingSource;}
static bool OpenExtractedArchive(ShadowReader*,const char*,const char*,const AssetSpec*){return false;}
static bool ReaderRead(ShadowReader*,u64,void*d,unsigned n){++sourceReads;sourceBytes+=n;memcpy(d,identity,n);return true;}
static bool ReaderRawRead(ShadowReader*,u64,void*,unsigned){return false;}
static bool SusamuneCisoMapInit(unsigned short*,const u8*,unsigned,u64){return false;}
static int FindArchiveInDisc(ShadowReader*,u64,const AssetSpec*,u64*offset,unsigned*size){++finds;*offset=0x1000;*size=originalSize;return 0;}
static bool DecodeArchive(ShadowReader*,u64,unsigned,const AssetSpec*spec,ShadowDecoder*,int*status){
 ++decodes;if(failDecode){*status=-4;return false;}memcpy((void*)spec->payload,original,originalSize);return true;}
'''
        code += function_source(path, 'static void StageAsset(')
        code += r'''
static AssetSpec spec;
#define API extern "C" __declspec(dllexport)
API void reset(){memset(files,0,sizeof(files));config.Config=0;faults=reads=readBytes=writes=writeBytes=decodes=finds=sourceReads=sourceBytes=published=closed=flushes=0;missingSource=failDecode=false;finalStatus=0;}
API void setCrc(uLong(*f)(uLong,const Bytef*,u32)){crc32=f;}
API void configure(const u8*data,unsigned bmd,unsigned btk,unsigned checksum,unsigned bc,unsigned tc,const u8*id){
 originalSize=bmd+btk;memcpy(original,data,originalSize);memcpy(identity,id,32);memset(payload,0xA5,sizeof(payload));
 memset(&spec,0,sizeof(spec));spec.label="Test";spec.payload=payload+16;spec.bmdSize=bmd;spec.btkSize=btk;spec.magic=btk?0x534D5348:0x534D5049;
 spec.payloadChecksum=checksum;spec.bmdChecksum=bc;spec.btkChecksum=tc;}
API int boot(unsigned flags){faults=flags;memset(payload,0xA5,sizeof(payload));StageAsset(&spec,"sd","/game.iso",0,0,false);return finalStatus;}
API int bootDevices(unsigned launchUsb,unsigned gameUsb){config.Config=launchUsb?NIN_CFG_CFG_ON_USB:0;StageAsset(&spec,gameUsb?"usb":"sd","/game.iso",0,0,false);return finalStatus;}
API unsigned devices(unsigned launchUsb,unsigned gameUsb){
 if(!equal(sourceDevice,gameUsb?"usb":"sd"))return 0;
 for(unsigned i=0;i<10;++i)if(files[i].exists&&memcmp(files[i].path,launchUsb?"usb:":"sd:/",4))return 0;
 return 1;
}
API int blocked(unsigned mode){missingSource=mode==1;StageAsset(&spec,"sd","/game.iso",0,0,mode==2);missingSource=false;return finalStatus;}
API void damage(unsigned mode){for(unsigned i=0;i<10;++i)if(files[i].exists&&!suffix(files[i].path,".tmp")){
 if(mode==0)files[i].bytes[files[i].size-1]^=1;
 else if(mode==1)--files[i].size;else if(mode==2)++files[i].size;
 else if(mode==3)files[i].bytes[8]^=1;else if(mode==4)text(files[i].path,"sd:/Moonshine data/cache/unpublished.tmp");
 else if(mode==5)files[i].bytes[32]^=1;
 break;}}
API void decodeFailure(unsigned on){failDecode=on!=0;}
API unsigned check(){return !memcmp(payload+16,original,originalSize)&&payload[0]==0xA5&&payload[15]==0xA5&&payload[16+originalSize]==0xA5;}
API unsigned count(unsigned key){switch(key){case 0:return decodes;case 1:return finds;case 2:return reads;case 3:return readBytes;case 4:return writes;case 5:return writeBytes;case 6:return sourceReads;case 7:return sourceBytes;case 8:return published;}return 0;}
API unsigned stored(){unsigned n=0;for(unsigned i=0;i<10;++i)if(files[i].exists&&!suffix(files[i].path,".tmp"))++n;return n;}
'''
        cpp = Path(cls.temp.name) / 'cache.cpp'
        cpp.write_text(code)
        dll = cpp.with_suffix('.dll')
        subprocess.run([str(compiler), '--target=x86_64-pc-windows-msvc', '-shared',
                        '-O2', '-nostdlib', '-fno-builtin', '-fuse-ld=lld', '-Wl,/noentry',
                        str(cpp), '-o', str(dll)], check=True, capture_output=True)
        cls.lib = C.CDLL(str(dll))
        cls.addClassCleanup(lambda: _ctypes.FreeLibrary(cls.lib._handle))
        callback = C.CFUNCTYPE(C.c_uint, C.c_uint, C.c_void_p, C.c_uint)
        cls.crc = callback(lambda seed, pointer, size: zlib.crc32(C.string_at(pointer, size), seed))
        cls.lib.setCrc.argtypes = [callback]
        cls.lib.setCrc(cls.crc)
        cls.lib.configure.argtypes = [C.c_void_p] + [C.c_uint] * 5 + [C.c_void_p]

    def setUp(self):
        self.lib.reset()
        self.configure()

    def configure(self, region='E', revision=0, bmd=0xF8C0, btk=0x440, raw=None):
        raw = bytes(shadow_checks.ShadowAssetChecksumTests.payload(bmd, btk)) if raw is None else raw
        identity = bytearray(32)
        identity[:8] = b'GMS' + region.encode() + b'01\0' + bytes([revision])
        struct.pack_into('>I', identity, 0x1c, 0xC2339F3D)
        self.lib.configure(raw, bmd, btk, zlib.crc32(raw), zlib.crc32(raw[:bmd]),
                           zlib.crc32(raw[bmd:]), bytes(identity))

    def test_first_launch_extracts_and_next_launch_uses_verified_cache(self):
        self.assertEqual(self.lib.boot(0), 1)
        self.assertEqual(self.lib.stored(), 1)
        self.assertEqual(self.lib.count(0), 1)
        self.assertEqual(self.lib.check(), 1)
        writes = self.lib.count(4)
        self.assertEqual(self.lib.boot(0), 1)
        self.assertEqual(self.lib.count(0), 1)
        self.assertEqual(self.lib.count(1), 1)
        self.assertEqual(self.lib.count(4), writes)
        self.assertEqual(self.lib.check(), 1)

    def test_region_revision_and_each_asset_have_separate_checked_keys(self):
        for region, revision, bmd, btk in [('J', 0, 0xF8C0, 0x440), ('E', 0, 0xF8C0, 0x440),
                                         ('P', 0, 0xF8C0, 0x440), ('E', 1, 0xF8C0, 0x440),
                                         ('E', 0, 0x119A0, 0)]:
            self.configure(region, revision, bmd, btk)
            before = self.lib.count(0)
            self.assertEqual(self.lib.boot(0), 1)
            self.assertEqual(self.lib.boot(0), 1)
            self.assertEqual(self.lib.count(0), before + 1)
            self.assertEqual(self.lib.check(), 1)
        self.assertEqual(self.lib.stored(), 5)

    def test_cache_stays_on_the_mounted_launcher_data_volume(self):
        for launcher_usb, game_usb in ((0, 1), (1, 0)):
            with self.subTest(launcher_usb=launcher_usb, game_usb=game_usb):
                self.setUp()
                self.assertEqual(self.lib.bootDevices(launcher_usb, game_usb), 1)
                self.assertEqual(self.lib.devices(launcher_usb, game_usb), 1)
                self.assertEqual(self.lib.stored(), 1)
                self.assertEqual(self.lib.bootDevices(launcher_usb, game_usb), 1)
                self.assertEqual(self.lib.count(0), 1)
                self.assertEqual(self.lib.check(), 1)

    def test_corrupt_truncated_oversized_wrong_key_and_temporary_fall_back(self):
        for mode in range(6):
            with self.subTest(mode=mode):
                self.setUp()
                self.assertEqual(self.lib.boot(0), 1)
                self.lib.damage(mode)
                self.assertEqual(self.lib.boot(0), 1)
                self.assertEqual(self.lib.count(0), 2)
                self.assertEqual(self.lib.check(), 1)
                self.assertEqual(self.lib.boot(0), 1)
                self.assertEqual(self.lib.count(0), 2)

    def test_each_cache_io_failure_keeps_the_good_extraction_and_boots(self):
        for fault in (1, 2, 4, 8, 16, 32, 64, 128, 256, 512):
            with self.subTest(fault=fault):
                self.setUp()
                self.assertEqual(self.lib.boot(fault), 1)
                self.assertEqual(self.lib.check(), 1)
                self.assertEqual(self.lib.stored(), 0)
                self.assertEqual(self.lib.boot(0), 1)
                self.assertEqual(self.lib.count(0), 2)
                self.assertEqual(self.lib.boot(0), 1)
                self.assertEqual(self.lib.count(0), 2)

    def test_bad_cache_never_publishes_when_source_extraction_also_fails(self):
        self.assertEqual(self.lib.boot(0), 1)
        self.lib.damage(0)
        self.lib.decodeFailure(1)
        published = self.lib.count(8)
        self.assertNotEqual(self.lib.boot(0), 1)
        self.assertEqual(self.lib.count(8), published)

    def test_valid_cache_does_not_bypass_source_open_or_unsupported_source_guards(self):
        self.assertEqual(self.lib.boot(0), 1)
        for mode in (1, 2):
            self.assertNotEqual(self.lib.blocked(mode), 1)
        self.assertEqual(self.lib.stored(), 1)


if __name__ == '__main__':
    unittest.main()
