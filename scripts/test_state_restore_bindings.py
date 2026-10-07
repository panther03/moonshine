"""Retain live render pointers while restoring every other byte, even split chunks."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RestoreBindingsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='moonshine-state-bindings-')
        cls.addClassCleanup(cls.temp.cleanup)
        path = Path(cls.temp.name) / 'bindings.cpp'
        path.write_text(r'''
#include "susamune/state_restore_bindings.hxx"
static StateRestoreBindings::Words words;
static unsigned char live[256], source[256];
static unsigned writes[256];
static const unsigned base=0x81000000;
static void copy(void*,void *dst,const void *src,unsigned size) {
 unsigned offset=reinterpret_cast<StateLiveVideo::Address>(dst)-base;
 for(unsigned i=0;i<size;++i){live[offset+i]=((const unsigned char*)src)[i];++writes[offset+i];}
}
#define API extern "C" __declspec(dllexport)
API void reset(){words.reset(base,base+256);for(unsigned i=0;i<256;++i){live[i]=i;source[i]=255-i;writes[i]=0;}}
API unsigned add(unsigned offset){return words.add((const void*)(StateLiveVideo::Address)(base+offset));}
API unsigned count(){return words.count;}
API unsigned value(unsigned offset){return live[offset];}
API unsigned written(unsigned offset){return writes[offset];}
API void restore(unsigned first,unsigned size,unsigned chunk){
 while(size){unsigned n=size<chunk?size:chunk;
 StateRestoreBindings::copyExcept(words,0,(void*)(StateLiveVideo::Address)(base+first),source+first,n,copy);
 first+=n;size-=n;}
}
''', encoding='ascii')
        lib = path.with_suffix('.dll')
        subprocess.run([str(ROOT/'toolchain/clang++.exe'), '--target=x86_64-pc-windows-msvc',
                        '-shared', '-nostdlib', '-fuse-ld=lld', '-Wl,/noentry', '-O2',
                        '-fno-builtin', '-I', str(ROOT/'include'), str(path), '-o', str(lib)], check=True)
        cls.lib = C.CDLL(str(lib))
        cls.addClassCleanup(lambda: C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def test_sorted_unique_words_and_adjacent_words_survive_every_chunk_boundary(self):
        keep = [124, 0, 12, 128, 252, 40, 12]
        for chunk in [1, 2, 3, 4, 7, 31, 128, 256]:
            self.lib.reset()
            for offset in keep:
                self.assertTrue(self.lib.add(offset))
            self.assertEqual(self.lib.count(), 6)
            self.lib.restore(0, 256, chunk)
            for i in range(256):
                retained = any(offset <= i < offset+4 for offset in keep)
                self.assertEqual(self.lib.value(i), i if retained else 255-i)
                self.assertEqual(self.lib.written(i), 0 if retained else 1)

    def test_partial_restore_keeps_pointer_fragments_and_untouched_outer_bytes(self):
        for first, size in [(11, 3), (13, 1), (14, 6), (10, 10), (20, 0)]:
            self.lib.reset(); self.assertTrue(self.lib.add(12))
            self.lib.restore(first, size, 3)
            for i in range(256):
                changed = first <= i < first+size and not 12 <= i < 16
                self.assertEqual(self.lib.value(i), 255-i if changed else i)

    def test_capacity_alignment_and_heap_boundaries_fail_without_mutation(self):
        self.lib.reset()
        for offset in [1, 2, 3, 253, 254, 255, 256, 0xffffffff]:
            self.assertFalse(self.lib.add(offset)); self.assertEqual(self.lib.count(), 0)
        for offset in range(0, 128, 4): self.assertTrue(self.lib.add(offset))
        self.assertFalse(self.lib.add(128)); self.assertEqual(self.lib.count(), 32)
        self.assertTrue(self.lib.add(4)); self.assertEqual(self.lib.count(), 32)

    def test_empty_keep_list_restores_all_bytes(self):
        self.lib.reset(); self.lib.restore(0, 256, 7)
        self.assertEqual([self.lib.value(i) for i in range(256)], list(reversed(range(256))))

    def test_binding_collection_and_drawing_reset_bracket_all_load_paths(self):
        from test_practice_tape import function_source
        load = function_source(ROOT/'src/savestate.cpp', 'bool SavestateManager::loadSlot(')
        self.assertLess(load.index('captureRestoreBindings('), load.index('StateCodec::decompress'))
        self.assertGreater(load.index('GhostModel::onSavestateLoaded()'), load.index('if (restored != StateCodec::SUCCESS)'))
        self.assertLess(load.index('GhostModel::onSavestateLoaded()'), load.index('GXInvalidateTexAll'))
