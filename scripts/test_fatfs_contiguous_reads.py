"""Execute the complete kernel FatFS reader against mapped, fragmented extents."""
import ctypes as C
from pathlib import Path
import random
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

FIXTURE = r'''
#include "ff.h"
#include "diskio.h"
void *memcpy(void*d,const void*s,__SIZE_TYPE__ n){BYTE*a=d;const BYTE*b=s;while(n--)*a++=*b++;return d;}
void *memset(void*d,int v,__SIZE_TYPE__ n){BYTE*a=d;while(n--)*a++=(BYTE)v;return d;}
static FATFS fs;
static FIL file;
static DWORD map[64];
static DWORD reads,writes,maxRead,badRead,failAt,logCount;
static DWORD sectors[20000],counts[20000];
static const BYTE *source;
static FSIZE_t sourceSize;
static DWORD physical(DWORD logical){
 DWORD *p=map+1;
 while(*p){if(logical<p[0])return p[1]+logical;logical-=p[0];p+=2;}return 0;
}
static FSIZE_t logical(DWORD physicalCluster){
 DWORD *p=map+1;FSIZE_t base=0;
 while(*p){if(physicalCluster>=p[1]&&physicalCluster-p[1]<p[0])return base+physicalCluster-p[1];base+=p[0];p+=2;}
 return ~(FSIZE_t)0;
}
DSTATUS disk_initialize(BYTE drive){return drive?STA_NODISK:0;}
DSTATUS disk_status(BYTE drive){return disk_initialize(drive);}
static DRESULT read_disk(BYTE drive,BYTE*out,DWORD sector,UINT count){
 if(drive)return RES_PARERR;
 if(sector<fs.database){
  if(sector<fs.fatbase)return RES_PARERR;
  for(UINT j=0;j<count*fs.ssize;j+=4){
   DWORD cl=(sector-fs.fatbase)*fs.ssize/4+j/4;
   FSIZE_t index=logical(cl);DWORD next=index==~(FSIZE_t)0?0:physical((DWORD)index+1);
   if(index!=~(FSIZE_t)0&&!next)next=0x0fffffff;
   out[j]=next;out[j+1]=next>>8;out[j+2]=next>>16;out[j+3]=next>>24;
  }return RES_OK;
 }
 ++reads;if(count*fs.ssize>maxRead)maxRead=count*fs.ssize;
 if(logCount<20000){sectors[logCount]=sector;counts[logCount++]=count;}
 if(failAt&&reads==failAt)return RES_ERROR;
 FSIZE_t initial=~(FSIZE_t)0;
 for(UINT i=0;i<count;i++){
  DWORD relative=sector+i-fs.database;
  FSIZE_t index=logical(relative/fs.csize+2);
  if(index==~(FSIZE_t)0){++badRead;return RES_PARERR;}
  FSIZE_t offset=(index*fs.csize+relative%fs.csize)*fs.ssize;
  if(i&&offset!=initial+(FSIZE_t)i*fs.ssize){++badRead;return RES_PARERR;}
  if(!i)initial=offset;
  if(source&&offset<sourceSize){
   UINT copied=sourceSize-offset<fs.ssize?(UINT)(sourceSize-offset):fs.ssize;
   memcpy(out+i*fs.ssize,source+offset,copied);
   memset(out+i*fs.ssize+copied,0,fs.ssize-copied);
  }else for(UINT j=0;j<fs.ssize;j++)out[i*fs.ssize+j]=(BYTE)((offset+j)*17+((offset+j)>>9));
 }
 return RES_OK;
}
static DRESULT write_disk(BYTE drive,const BYTE*in,DWORD sector,UINT count){++writes;return RES_OK;}
DiskReadFunc disk_read=read_disk;DiskWriteFunc disk_write=write_disk;
DRESULT disk_ioctl(BYTE drive,BYTE cmd,void*out){return RES_OK;}
DWORD get_fattime(void){return 0;}
__declspec(dllexport) void configure(unsigned ssize,unsigned cluster,unsigned mode,
 const DWORD* extents,unsigned extentCount,FSIZE_t size){
 memset(&fs,0,sizeof(fs));memset(&file,0,sizeof(file));memset(map,0,sizeof(map));
 fs.fs_type=3;fs.id=1;fs.ssize=ssize;fs.csize=cluster;fs.n_fatent=0x1000000;
 fs.fatbase=1;fs.database=0x20000;fs.winsect=0xffffffff;
 map[0]=extentCount*2+2;memcpy(map+1,extents,extentCount*8);
 file.obj.fs=&fs;file.obj.id=1;file.obj.sclust=map[2];file.obj.objsize=size;
 file.flag=FA_READ|((mode&2)?FA_WRITE:0);file.cltbl=(mode&1)?map:0;
 reads=writes=maxRead=badRead=failAt=logCount=0;source=0;sourceSize=0;
}
__declspec(dllexport) void set_source(const BYTE* bytes,FSIZE_t length){source=bytes;sourceSize=length;}
__declspec(dllexport) int seek(FSIZE_t offset){return f_lseek(&file,offset);}
__declspec(dllexport) int take(BYTE*out,unsigned length,unsigned*actual){return f_read(&file,out,length,actual);}
__declspec(dllexport) int put(const BYTE*in,unsigned length,unsigned*actual){return f_write(&file,in,length,actual);}
__declspec(dllexport) void fail(unsigned attempt){failAt=attempt;}
__declspec(dllexport) FSIZE_t stat(unsigned key){
 switch(key){case 0:return reads;case 1:return writes;case 2:return maxRead;case 3:return badRead;
 case 4:return file.fptr;case 5:return file.clust;case 6:return file.err;default:return logCount;}
}
__declspec(dllexport) DWORD log_value(unsigned i,unsigned field){return field?counts[i]:sectors[i];}
'''


def compile_reader(folder, source=None):
    folder = Path(folder)
    fixture = folder / 'reader.c'
    fixture.write_text(FIXTURE, encoding='ascii')
    library = folder / 'reader.dll'
    fatfs = ROOT / 'launcher/fatfs'
    result = subprocess.run([str(ROOT / 'toolchain/clang.exe'),
                             '--target=x86_64-pc-windows-msvc', '-U_WIN32',
                             '-shared', '-nostdlib', '-fno-builtin', '-O2',
                             '-fuse-ld=lld', '-Wl,/noentry', '-I', str(fatfs),
                             str(fixture), str(source or fatfs / 'ff.c'),
                             str(fatfs / 'option/ccsbcs.c'), '-o', str(library)],
                            capture_output=True, text=True)
    if result.returncode:
        raise AssertionError(result.stderr)
    lib = C.CDLL(str(library))
    lib.configure.argtypes = [C.c_uint, C.c_uint, C.c_uint, C.c_void_p, C.c_uint, C.c_uint64]
    lib.set_source.argtypes = [C.c_void_p, C.c_uint64]
    lib.seek.argtypes = [C.c_uint64]
    lib.take.argtypes = lib.put.argtypes = [C.c_void_p, C.c_uint, C.POINTER(C.c_uint)]
    lib.stat.argtypes = [C.c_uint]
    lib.stat.restype = C.c_uint64
    return lib


class FatFsContiguousReadsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (ROOT / 'toolchain/clang.exe').exists():
            raise unittest.SkipTest('Bundled Windows compiler required')
        cls.temp = tempfile.TemporaryDirectory(prefix='moonshine-fatfs-reads-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.lib = compile_reader(cls.temp.name)
        cls.addClassCleanup(lambda: C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def configure(self, *, ssize=512, cluster=64, mode=1, extents=None, size=None):
        extents = extents or [(128, 2)]
        flat = [value for pair in extents for value in pair]
        self.size = size if size is not None else sum(a for a, _ in extents)*cluster*ssize
        self.lib.configure(ssize, cluster, mode, (C.c_uint*len(flat))(*flat), len(extents), self.size)

    def take(self, size, offset=None):
        if offset is not None:
            self.assertEqual(self.lib.seek(offset), 0)
        offset = self.lib.stat(4)
        output = C.create_string_buffer(b'\xa7'*(size+64))
        actual = C.c_uint()
        result = self.lib.take(C.byref(output, 32), size, C.byref(actual))
        self.assertEqual(output.raw[:32], b'\xa7'*32)
        self.assertEqual(output.raw[32+size:32+size+32], b'\xa7'*32)
        if result == 0:
            expected = bytes(((i*17+(i>>9))&255) for i in range(offset, offset+actual.value))
            self.assertEqual(output.raw[32:32+actual.value], expected)
            self.assertEqual(actual.value, min(size, self.size-offset))
        self.assertEqual(self.lib.stat(3), 0, 'device read crossed a noncontiguous extent')
        return result, actual.value

    def test_contiguous_large_read_uses_bounded_combined_requests(self):
        self.configure()
        self.assertEqual(self.take(256*1024), (0,256*1024))
        self.assertEqual(self.lib.stat(0), 4)
        self.assertEqual(self.lib.stat(2), 65536)
        self.assertEqual(self.lib.stat(5), 9)

    def test_fragment_boundaries_and_following_reads_keep_correct_cluster(self):
        self.configure(cluster=8, extents=[(3,2),(5,400),(1,90),(8,800)])
        self.take(17*4096)
        self.assertEqual(self.lib.stat(0), 4)
        self.take(33, 3*4096-17)
        self.take(4096, 8*4096)
        self.take(31, 9*4096)

    def test_no_linkmap_retains_cluster_reads(self):
        self.configure(mode=0, extents=[(3,2),(5,400),(8,900)])
        self.take(256*1024)
        self.assertEqual(self.lib.stat(0), 8)
        self.assertEqual(self.lib.stat(2), 32768)

    def test_writable_linkmap_retains_cluster_reads_and_writes(self):
        self.configure(mode=3)
        self.take(256*1024)
        self.assertEqual(self.lib.stat(0), 8)
        self.assertEqual(self.lib.stat(2), 32768)
        actual = C.c_uint()
        self.assertEqual(self.lib.put(b'\0'*65536, 65536, C.byref(actual)), 0)
        self.assertEqual(actual.value, 65536)
        self.assertEqual(self.lib.stat(1), 2)

    def test_sector_sizes_and_unaligned_partial_reads(self):
        for ssize in (512,1024,2048,4096):
            for cluster in (1,8,64,128):
                with self.subTest(ssize=ssize, cluster=cluster):
                    self.configure(ssize=ssize, cluster=cluster, extents=[(2,2),(7,90),(99,800)])
                    self.take(93001, ssize-29)
                    self.take(71)
                    self.assertLessEqual(self.lib.stat(2), 65536)

    def test_eof_zero_reads_and_small_tail(self):
        self.configure(size=131111)
        self.assertEqual(self.take(0), (0,0))
        self.assertEqual(self.lib.stat(0), 0)
        self.take(131072)
        self.assertEqual(self.take(1000), (0,39))
        self.assertEqual(self.take(1000), (0,0))

    def test_error_keeps_completed_prefix_and_latches_disk_error(self):
        self.configure()
        self.lib.fail(2)
        self.assertEqual(self.take(3*65536), (1,65536))
        self.assertEqual(self.lib.stat(4), 65536)
        self.assertEqual(self.lib.stat(6), 1)
        self.assertEqual(self.take(512), (1,0))

    def test_random_seeks_across_fragmented_linkmap(self):
        self.configure(cluster=8, extents=[(7,900),(3,2),(29,100),(33,3000)], size=71*4096+13)
        rng = random.Random(233)
        for _ in range(100):
            self.take(rng.randrange(0,90000), rng.randrange(0,self.size))
        self.assertEqual(self.lib.stat(1),0)

    def test_large_extent_is_bounded_before_multiplication(self):
        self.configure(extents=[(0xfffffff,2)],size=(1<<40)-1)
        self.take(65536, 4096)
        self.assertLessEqual(self.lib.stat(2),65536)


if __name__ == '__main__':
    unittest.main()
