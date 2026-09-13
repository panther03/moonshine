"""A restore must not rewind memory still owned by the THP decoder threads."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_practice_tape import function_source

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/savestate.cpp"


class LiveVideoRestoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        source = r'''
#include "susamune/state_live_video.hxx"
#include "susamune/state_restore_bindings.hxx"
static StateRestoreBindings::Words sRestoreBindings = {};
typedef unsigned int u32;
static unsigned int player[0x1d0/4], needed;
static unsigned char *ActivePlayer=reinterpret_cast<unsigned char*>(player);
static StateLiveVideo::Range sLiveVideo,sVideoReadRing;
static StateLiveVideo::Address invalidated;
static unsigned invalidatedSize;
static void DCInvalidateRange(void*p,unsigned n){invalidated=(StateLiveVideo::Address)p;invalidatedSize=n;}
static unsigned int THPPlayerCalcNeedMemory(){return needed;}
static void *memcpy(void *d,const void *s,unsigned n){
 unsigned char *dst=(unsigned char*)d;const unsigned char *src=(const unsigned char*)s;
 for(unsigned i=0;i<n;++i)dst[i]=src[i];return d;
}
static unsigned char live[256],saved[256];
namespace PracticeSession {bool copySavestateBytes(void*,const void*,unsigned){return false;}}
namespace StateArchiveProfile {
void copyGameBytes(void*,void*d,const void*s,unsigned n){
 unsigned char *dst=(unsigned char*)d;const unsigned char *src=(const unsigned char*)s;
 for(unsigned i=0;i<n;++i)if((dst+i-live)%7)dst[i]=src[i];
}}
''' + function_source(SOURCE, "bool captureLiveVideo(") + "\n" + \
            function_source(SOURCE, "void invalidateVideoReadBuffer(") + "\n" + \
            function_source(SOURCE, "void copyBaseStateBytes(") + "\n" + \
            function_source(SOURCE, "void copyOwnedStateBytes(") + "\n" + \
            function_source(SOURCE, "void copyStateBytes(") + r'''
#define API extern "C" __declspec(dllexport)
API unsigned range(unsigned open,unsigned memory,unsigned base,unsigned work,unsigned size){
 for(unsigned i=0;i<sizeof(player)/sizeof(*player);++i)player[i]=0;
 player[0xA0/4]=open;player[0xB0/4]=memory;player[(memory?0xB4:0x100)/4]=base;
 player[0x9C/4]=work;player[0x44/4]=0x2000;needed=size;return captureLiveVideo();
}
API unsigned bound(unsigned last){return last?sLiveVideo.last:sLiveVideo.first;}
API unsigned ring(unsigned bytes){return StateLiveVideo::readRingRange(bytes,sLiveVideo,sVideoReadRing);}
API unsigned flush(unsigned size){
 invalidated=invalidatedSize=0;invalidateVideoReadBuffer();return size?invalidatedSize:invalidated;
}
API void restore(unsigned first,unsigned size,unsigned keepFirst,unsigned keepSize,unsigned profile){
 for(unsigned i=0;i<256;++i){live[i]=(unsigned char)i;saved[i]=(unsigned char)(255-i);}
 sLiveVideo={reinterpret_cast<StateLiveVideo::Address>(live+keepFirst),
             reinterpret_cast<StateLiveVideo::Address>(live+keepFirst+keepSize)};
 copyStateBytes(profile?(void*)1:0,live+first,saved+first,size);
}
API unsigned byte(unsigned index){return live[index];}
'''
        path = Path(cls.temp.name) / "video.cpp"
        path.write_text(source, encoding="ascii")
        library = path.with_suffix(".dll")
        subprocess.run([str(ROOT / "toolchain/clang++.exe"),
                        "--target=x86_64-pc-windows-msvc", "-shared", "-nostdlib",
                        "-fuse-ld=lld", "-Wl,/noentry", "-O2", "-fno-builtin", "-I", str(ROOT / "include"),
                        str(path), "-o", str(library)], check=True)
        cls.lib = C.CDLL(str(library))
        cls.addClassCleanup(lambda: C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def test_closed_player_has_no_preserved_heap_range(self):
        self.assertTrue(self.lib.range(0, 0, 0, 0, 0))
        self.assertEqual([self.lib.bound(i) for i in (0, 1)], [0, 0])

    def test_streamed_and_memory_video_use_current_complete_buffer(self):
        for memory in (0, 1):
            self.assertTrue(self.lib.range(1, memory, 0x81000000, 0x81020000, 0x21000))
            self.assertEqual([C.c_uint(self.lib.bound(i)).value for i in (0, 1)],
                             [0x81000000, 0x81021000])

    def test_invalid_live_buffer_is_refused_before_restore(self):
        cases = [(2, 0, 0x81000000, 0x81020000, 0x21000),
                 (1, 2, 0x81000000, 0x81020000, 0x21000),
                 (1, 0, 0x71000000, 0x71020000, 0x21000),
                 (1, 0, 0x81000001, 0x81020000, 0x21000),
                 (1, 0, 0x817FF000, 0x81800000, 0x2000),
                 (1, 0, 0x81000000, 0x81020000, 0xFFFFFFFF),
                 (1, 0, 0x81000000, 0x81000000, 0xFFF),
                 (1, 0, 0x81000000, 0x80FFFFE0, 0x2000),
                 (1, 0, 0x81000000, 0x81020000, 0x20FE0),
                 (1, 0, 0x81000000, 0x81020000, 0x21020)]
        for args in cases:
            with self.subTest(args=args):
                self.assertFalse(self.lib.range(*args))

    def test_ram_sd_and_recovery_copies_keep_live_video_and_owner_bytes(self):
        for first, size in ((0, 64), (192, 64), (64, 128), (80, 48),
                            (32, 80), (96, 112), (0, 256)):
            for profile in (0, 1):
                with self.subTest(first=first, size=size, profile=profile):
                    self.lib.restore(first, size, 64, 128, profile)
                    expected = [255-i if first <= i < first+size and not 64 <= i < 192
                                and (not profile or i % 7) else i for i in range(256)]
                    self.assertEqual([self.lib.byte(i) for i in range(256)], expected)

    def test_save_invalidates_only_streaming_input_and_keeps_decoder_work(self):
        self.assertTrue(self.lib.range(1, 0, 0x81000000, 0x81020000, 0x21000))
        self.assertEqual(C.c_uint(self.lib.flush(0)).value, 0x81000000)
        self.assertEqual(self.lib.flush(1), 0x14000)
        self.assertTrue(self.lib.ring(33))
        self.assertEqual(self.lib.flush(1), 640)
        for size in (0, 0xFFFFFFFF, 0x3334):
            with self.subTest(size=size):
                self.assertFalse(self.lib.ring(size))
                self.assertEqual(self.lib.flush(1), 0)
        self.assertTrue(self.lib.range(1, 1, 0x81000000, 0x81020000, 0x21000))
        self.assertEqual(self.lib.flush(1), 0)

    def test_save_releases_input_cache_before_interrupts_on_success_and_failures(self):
        body = function_source(SOURCE, "bool SavestateManager::saveSlotExplicit(")
        self.assertLess(body.index("OSDisableInterrupts()"), body.index("captureLiveVideo()"))
        self.assertLess(body.index("captureLiveVideo()"), body.index("captureRegion("))
        exits = body[body.index("captureRegion("):].split("OSRestoreInterrupts(ints);")
        self.assertEqual(len(exits), 5)
        for block in exits[:-1]:
            self.assertLess(block.rindex("invalidateVideoReadBuffer();"),
                            block.rindex("unmuteAudioDma(dma);"))

    def test_all_restore_paths_capture_live_buffer_with_interrupts_disabled(self):
        body = function_source(SOURCE, "bool SavestateManager::loadSlot(u32 slot,")
        guard = body.index("if (!captureLiveVideo())")
        self.assertLess(body.index("OSDisableInterrupts()"), guard)
        self.assertLess(guard, body.index("StateCodec::decompress"))
        self.assertIn('feedback("E:video", "Video player busy - try again")', body)
        self.assertEqual(body.count("copyStateBytes,"), 4)


if __name__ == "__main__":
    unittest.main()
