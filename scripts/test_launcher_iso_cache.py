"""Execute production ISO caching and memory-card admission against guarded RAM."""
import ctypes as C
from pathlib import Path
import random
import subprocess
import tempfile
import unittest

from test_practice_tape import function_source

ROOT=Path(__file__).resolve().parents[1]

FIXTURE=r'''
typedef unsigned int u32;typedef unsigned char u8;typedef unsigned long long u64;
typedef unsigned int UINT;
extern "C" void *memcpy(void*d,const void*s,__SIZE_TYPE__ n){u8*a=(u8*)d;const u8*b=(const u8*)s;while(n--)*a++=*b++;return d;}
extern "C" void *memset(void*d,int c,__SIZE_TYPE__ n){u8*a=(u8*)d;while(n--)*a++=(u8)c;return d;}
#define memset32 memset
#define dbgprintf(...) ((void)0)
#define NIN_MEM2_DISC_CACHE_SIZE 0x300000u
#define CACHE_MAX 0x400u
#define CACHE_SIZE NIN_MEM2_DISC_CACHE_SIZE
#define CACHE_START (memory+32)
#define GCNCARD_ENABLE_SLOT_B 1
#define NIN_CFG_MEMCARDEMU 1
#define NIN_CFG_MC_MULTI 2
#define NIN_CFG_MC_SLOTB 4
#define BI2_REGION_JAPAN 0
#define BI2_REGION_SOUTH_KOREA 5
#define MEM_CARD_MAX 5
#define MEM_CARD_SIZE(n) (1u<<((n)+19))
#define MEM_CARD_CODE(n) (1u<<((n)+2))
#define FA_READ 1
#define FA_OPEN_EXISTING 0
#define FR_OK 0
alignas(32) static u8 memory[CACHE_SIZE+64],scratch[0x80000+64];
static u8*const DI_READ_BUFFER=scratch+32;
static const u32 DI_READ_BUFFER_LENGTH=0x80000;
static u32 ISOFileOpen=1,CacheInited,TempCacheCount,CacheEntryCount,DataCacheOffset;
static u8*DCCache=CACHE_START;
static u32 DCacheLimit=CACHE_SIZE,TRIGame,BI2region;
static u64 ISOShift64,LastOffset64;
struct DataCache{u32 Offset,Size;u8*Data;};static DataCache DC[CACHE_MAX];
struct GCNCard_ctx{char filename[0x20];u8*base;u32 size,code,BlockOffLow,BlockOffHigh;};
static GCNCard_ctx memCard[2];
static u8*const GCNCard_base=CACHE_START;
struct Config{u32 Config;};static struct Config config;static struct Config*ncfg=&config;
static u32 cardSizes[2],openingSlot,cardReads,cardCloses,shutdowns,configWrites;
static int bootStatus,bootError;
struct FIL{struct {u64 objsize;}obj;};
static int f_open_char(FIL*f,const char*,int){f->obj.objsize=cardSizes[openingSlot];return 0;}
static void f_close(FIL*){++cardCloses;}
static void f_lseek(FIL*,u32){}
static void f_read(FIL*,void*dst,u32 n,UINT*read){++cardReads;memset(dst,0x78,n);*read=n;}
static void sync_after_write(const void*,u32){}
static void mdelay(u32){}
static void Shutdown(){++shutdowns;}
static void BootStatusError(int status,int error){bootStatus=status;bootError=error;}
static u32 ConfigGetConfig(u32 flag){return ncfg->Config&flag;}
static u32 ConfigGetGameID(){return 0x474D5350u;}
static void ConfigSetMemcardBlocks(u32){++configWrites;}
static u32 GCNCard_GetTotalSize(){return memCard[0].size+memCard[1].size;}
static u32 reads,bytesRead,lastReadSize,lastReturned,overruns,lastDestination;
static u64 lastReadOffset;
static u8 sample(u64 address){return (u8)(address^(address>>8)^(address>>16)^(address>>32));}
static void ISOReadDirect(void*dst,u32 length,u64 offset){
 ++reads;bytesRead+=length;lastReadSize=length;lastReadOffset=offset;
 lastDestination=(u32)((u8*)dst-CACHE_START);
 u8*out=(u8*)dst;
 bool inCache=out>=CACHE_START&&out<=CACHE_START+CACHE_SIZE&&length<=(u32)(CACHE_START+CACHE_SIZE-out);
 bool inScratch=out==DI_READ_BUFFER&&length<=DI_READ_BUFFER_LENGTH;
 if(!inCache&&!inScratch){++overruns;return;}
 for(u32 i=0;i<length;++i)out[i]=sample(offset+i);
 LastOffset64=offset+length;
}
'''

EXPORTS=r'''
#define API extern "C" __declspec(dllexport)
API void reset(u32 cardBytes,u32 cached){
 memset(memory,0xA5,sizeof(memory));memset(scratch,0xB6,sizeof(scratch));
 memset(DC,0,sizeof(DC));memset(memCard,0,sizeof(memCard));
 ISOFileOpen=1;CacheInited=TempCacheCount=CacheEntryCount=DataCacheOffset=0;
 DCCache=CACHE_START;DCacheLimit=CACHE_SIZE;LastOffset64=~0ull;ISOShift64=0;
 reads=bytesRead=lastReadSize=lastReturned=overruns=0;TRIGame=0;
 memCard[0].size=cardBytes;config.Config=1;
 cardReads=cardCloses=shutdowns=configWrites=0;bootStatus=bootError=0;
 if(cached)ISOSetupCache();
}
API void shift(u64 value){ISOShift64=value;}
API void sequential(u32 value){LastOffset64=ISOShift64+value;}
API u32 readAt(u32 offset,u32 length){
 const u8*out=ISORead(&length,offset);lastReturned=length;
 if(overruns)return 0;
 for(u32 i=0;i<length;++i)if(out[i]!=sample(ISOShift64+offset+i))return 0;
 return 1;
}
API u32 guards(){for(u32 i=0;i<32;++i){
 if(memory[i]!=0xA5||memory[32+CACHE_SIZE+i]!=0xA5||scratch[i]!=0xB6||scratch[32+DI_READ_BUFFER_LENGTH+i]!=0xB6)return 0;}
 return 1;}
API int card(u32 slot,u32 size){openingSlot=slot;cardSizes[slot]=size;return GCNCard_Load(slot);}
API u32 get(u32 key){switch(key){case 0:return reads;case 1:return bytesRead;case 2:return lastReturned;
 case 3:return lastReadSize;case 4:return DCacheLimit;case 5:return cardReads;case 6:return cardCloses;
 case 7:return shutdowns;case 8:return configWrites;case 9:return (u32)bootStatus;case 10:return (u32)bootError;
 case 11:return overruns;case 12:return memCard[0].size;case 13:return memCard[1].size;case 14:return CacheEntryCount;
 case 15:return (u32)lastReadOffset;case 16:return (u32)(lastReadOffset>>32);
 case 17:return lastDestination;}return 0;}
'''


class LauncherIsoCacheTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=ROOT/'toolchain/clang++.exe'
        if not compiler.exists():raise unittest.SkipTest('Bundled Windows compiler required')
        cls.temp=tempfile.TemporaryDirectory(prefix='moonshine-iso-cache-')
        cls.addClassCleanup(cls.temp.cleanup)
        source=FIXTURE
        for path,name in (('GCNCard.c','static void GCNCard_InitCtx('),
                          ('GCNCard.c','int GCNCard_Load('),
                          ('ISO.c','void ISOSetupCache()'),('ISO.c','const u8 *ISORead(')):
            source+=function_source(ROOT/'launcher/kernel'/path,name)
        path=Path(cls.temp.name)/'iso.cpp'
        path.write_text(source+EXPORTS)
        library=path.with_suffix('.dll')
        subprocess.run([str(compiler),'--target=x86_64-pc-windows-msvc','-shared','-O2',
                        '-nostdlib','-fno-builtin','-fuse-ld=lld','-Wl,/noentry',
                        str(path),'-o',str(library)],check=True)
        cls.lib=C.CDLL(str(library))
        cls.addClassCleanup(lambda:C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))
        cls.lib.get.restype=C.c_uint
        cls.lib.shift.argtypes=[C.c_ulonglong]
        cls.lib.readAt.argtypes=[C.c_uint,C.c_uint]

    def read(self,offset,length):
        self.assertEqual(self.lib.readAt(offset,length),1,'Reader must return the correct disc bytes')
        self.assertEqual(self.lib.guards(),1,'Reader crossed its RAM reservation')
        self.assertEqual(self.lib.get(11),0)
        self.assertLessEqual(self.lib.get(2),length,'Read-ahead leaked into the caller output size')

    def test_wrapping_preserves_unmodified_recent_entries(self):
        self.lib.reset(0,1)
        for offset in (0x100000,0x300000,0x500000):self.read(offset,0x100000)
        self.read(0x800000,0x10000) # Wrap and overwrite the oldest entry only.
        before=self.lib.get(0)
        self.read(0x300000,0x100000);self.read(0x500000,0x100000)
        self.assertEqual(self.lib.get(0),before)
        self.read(0x100000,0x100000)
        self.assertEqual(self.lib.get(0),before+1)

    def test_oversized_read_streams_through_existing_scratch(self):
        self.lib.reset(0x100000,1)
        self.read(0x1000000,0x700000)
        self.assertEqual(self.lib.get(2),0x80000)
        self.assertEqual(self.lib.get(3),0x80000)
        self.assertEqual(self.lib.get(14),0)

    def test_exhausted_card_reservation_does_not_underflow_cache(self):
        for size in (0x300000,0x400000,0xFFFFFFFF):
            self.lib.reset(size,1)
            self.assertEqual(self.lib.get(4),0)
            self.read(0xA00000,0x80001)
            self.assertEqual(self.lib.get(2),0x80000)

    def test_uncached_and_cached_reads_apply_full_multidisc_shift(self):
        for cached in (0,1):
            self.lib.reset(0,cached)
            self.lib.shift(0x120000000)
            self.read(0x200000,0x1000)

    def test_read_ahead_keeps_caller_length_and_serves_next_read(self):
        self.lib.reset(0,1)
        self.lib.sequential(0x900000)
        self.read(0x900000,0x1000)
        self.assertEqual(self.lib.get(2),0x1000)
        self.assertEqual(self.lib.get(3),0x10000)
        before=self.lib.get(0)
        self.read(0x901000,0x1000)
        self.assertEqual(self.lib.get(0),before)

    def test_zero_read_and_metadata_slot_wrap_are_bounded(self):
        self.lib.reset(0,1)
        self.lib.sequential(0x4000)
        self.read(0x4000,0)
        self.assertEqual(self.lib.get(0),0)
        for index in range(1030):self.read(index*0x1000,32)
        self.assertEqual(self.lib.get(14),1024)
        self.read(1029*0x1000,32)

    def test_boundary_request_consumes_cached_prefix_without_rereading_it(self):
        self.lib.reset(0,1)
        self.lib.sequential(0x900020)
        self.read(0x900020,0x1000)
        self.assertEqual((self.lib.get(15),self.lib.get(3)),(0x900000,0x10000))
        fills=self.lib.get(0)
        self.read(0x90f020,0x1000)
        self.assertEqual((self.lib.get(0),self.lib.get(2)),(fills,0xfe0))
        self.read(0x910000,0x20)
        self.assertEqual(self.lib.get(0),fills+1)
        self.assertEqual((self.lib.get(15),self.lib.get(3)),(0x910000,0x10000))

    def test_alignment_never_adds_trailing_bytes_to_a_nonprefetched_read(self):
        self.lib.reset(0,1)
        self.read(0x903ffe,33)
        self.assertEqual((self.lib.get(15),self.lib.get(3)),(0x903e00,543))
        self.assertEqual(self.lib.get(15)+self.lib.get(3),0x903ffe+33)

    def test_unaligned_shift_preserves_logical_start_and_full_64bit_address(self):
        self.lib.reset(0,1);self.lib.shift(0x120000023)
        self.read(0,32) # Do not prepend bytes before the selected disc image.
        self.assertEqual((self.lib.get(16),self.lib.get(15),self.lib.get(3)),(1,0x20000023,32))
        self.read(0x4000,0x1000)
        self.assertEqual((self.lib.get(16),self.lib.get(15)),(1,0x20004000))
        fills=self.lib.get(0)
        self.read(0x4080,32)
        self.assertEqual(self.lib.get(0),fills)

    def test_short_fill_keeps_following_dma_aligned_without_padding_disc_read(self):
        self.lib.reset(0,1)
        self.read(0x903ffe,33)
        self.assertEqual((self.lib.get(3),self.lib.get(17)),(543,0))
        self.read(0xA00000,0x1000)
        self.assertEqual(self.lib.get(17),544)
        fills=self.lib.get(0)
        self.read(0x903ffe,33)
        self.assertEqual(self.lib.get(0),fills)

    def test_alignment_padding_at_ring_end_wraps_without_overwriting_adjacent_entries(self):
        self.lib.reset(0x200000,1)
        self.read(0x900000,33)
        self.read(0xA00000,0x100000-65)
        self.assertEqual(self.lib.get(17),0x200000+64)
        self.read(0xC00000,1)
        self.assertEqual(self.lib.get(17),0x200000)
        fills=self.lib.get(0)
        self.read(0xA00000,0x100000-65)
        self.assertEqual(self.lib.get(0),fills)
        self.read(0x900000,33)
        self.assertEqual(self.lib.get(0),fills+1)

    def test_prefix_cannot_overflow_a_completely_filled_cache(self):
        self.lib.reset(0x200000,1)
        self.read(0x900020,0x100000)
        self.assertEqual((self.lib.get(15),self.lib.get(3)),(0x900020,0x100000))
        self.assertEqual(self.lib.get(2),0x100000)

    def test_partial_cache_hits_always_progress_through_a_full_request(self):
        self.lib.reset(0,1)
        for offset in range(0x900000,0x920000,0x2000):self.read(offset,0x800)
        offset=0x900400;remaining=0x20000
        iterations=0
        while remaining:
            self.read(offset,remaining)
            consumed=self.lib.get(2)
            self.assertGreater(consumed,0)
            remaining-=consumed;offset+=consumed;iterations+=1
            self.assertLess(iterations,35)

    def test_randomized_overlapping_reads_return_exact_bytes_after_wraps(self):
        rng=random.Random(233)
        self.lib.reset(0x100000,1)
        for _ in range(240):
            offset=rng.randrange(0x100000,0x1200000,32)
            size=rng.choice((31,33,543,4096,0x8000,0x10000,0x30000,0x300001))
            self.read(offset,size)
            if size <= self.lib.get(4):
                self.assertEqual(self.lib.get(17)&31,0)

    def test_supported_cards_keep_their_full_data_and_both_slots_fit(self):
        self.lib.reset(0,0)
        self.assertEqual(self.lib.card(0,0x200000),0)
        self.assertEqual(self.lib.card(1,0x100000),0)
        self.assertEqual((self.lib.get(12),self.lib.get(13)),(0x200000,0x100000))
        self.assertEqual(self.lib.get(5),2)
        self.assertEqual(self.lib.get(7),0)
        self.assertEqual(self.lib.guards(),1)

    def test_oversized_existing_card_is_refused_before_any_copy(self):
        for size in (0x400000,0x800000,0x1000000):
            self.lib.reset(0,0)
            self.assertEqual(self.lib.card(0,size),-5)
            self.assertEqual(self.lib.get(5),0)
            self.assertEqual(self.lib.get(8),0)
            self.assertEqual(self.lib.get(12),0)
            self.assertEqual(self.lib.get(7),1)
            self.assertEqual(self.lib.get(9),0xFFFFFFF6)
            self.assertEqual(self.lib.get(10),0xFFFFFFFB)
            self.assertEqual(self.lib.guards(),1)

    def test_two_individually_valid_cards_cannot_overlap_state_staging(self):
        self.lib.reset(0,0)
        self.assertEqual(self.lib.card(0,0x200000),0)
        self.assertEqual(self.lib.card(1,0x200000),-5)
        self.assertEqual(self.lib.get(5),1)
        self.assertEqual(self.lib.get(13),0)
        self.assertEqual(self.lib.guards(),1)


if __name__=='__main__':unittest.main()
