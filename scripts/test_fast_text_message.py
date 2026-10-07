"""Exercise the forced dialogue fallback without relying on heap addresses."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_iling_attempt_lifecycle import function

ROOT=Path(__file__).resolve().parents[1]


class FastTextMessageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='moonshine-fast-text-')
        cls.addClassCleanup(cls.temp.cleanup)
        source=(ROOT/'src/features.cpp').read_text()
        # PPC pointers are 32 bits; use the host pointer width only for the
        # address arithmetic while leaving the retail entry offset 32-bit.
        body=function(source,'setupFastTextBox').replace(
            'reinterpret_cast<u32>(message)','reinterpret_cast<uintptr_t>(message)')
        fixture=r'''
using u32=unsigned;using uintptr_t=__UINTPTR_TYPE__;
bool enabled,suppressed;int calls;const void *seenTalk;const u32 *seenEntry;
const char *seenText;constexpr int SETTING_FAST_TEXT=1;
bool featureEnabled(int){return enabled&&!suppressed;}
void retailSetupTextBox(void*talk,const void*data,const u32*entry){
 ++calls;seenTalk=talk;seenEntry=entry;
 seenText=reinterpret_cast<const char*>(reinterpret_cast<uintptr_t>(data)+*entry);
}
'''
        exports=r'''
#define API extern "C" __declspec(dllexport)
API void apply(int on,int skip,void*talk,const void*text,const u32*entry){
 enabled=on;suppressed=skip;calls=0;
 const void*base=reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(text)-*entry);
 setupFastTextBox(talk,base,entry);
}
API const char*text(){return seenText;}
API const void*talk(){return seenTalk;}
API const u32*entry(){return seenEntry;}
API int count(){return calls;}
'''
        cls.libs={}
        for region in ('JP','US','PAL'):
            path=Path(cls.temp.name)/(region+'.cpp');path.write_text(fixture+body+exports)
            result=subprocess.run([str(ROOT/'toolchain/clang++.exe'),
                '--target=x86_64-pc-windows-msvc','-shared','-nostdlib','-fuse-ld=lld',
                '-Wl,/noentry','-O2','-DSUSAMUNE_VERSION_'+region+'=1',str(path),
                '-o',str(path.with_suffix('.dll'))],capture_output=True,text=True)
            if result.returncode:raise RuntimeError(result.stdout+result.stderr)
            lib=C.CDLL(str(path.with_suffix('.dll')))
            lib.apply.argtypes=[C.c_int,C.c_int,C.c_void_p,C.c_void_p,C.c_void_p]
            lib.text.restype=C.c_char_p
            lib.talk.restype=lib.entry.restype=C.c_void_p
            cls.libs[region]=lib
            cls.addClassCleanup(lambda lib=lib:C.windll.kernel32.FreeLibrary(C.c_void_p(lib._handle)))

    def invoke(self,lib,on,suppress,offset):
        entry=(C.c_uint*3)(offset,0x12345678,0x90abcdef)
        text=C.create_string_buffer(b'!!!ERROR!!! Message could not be loaded')
        original=bytes(entry)
        lib.apply(on,suppress,0x1234,text,entry)
        self.assertEqual(lib.count(),1)
        self.assertEqual(lib.talk(),0x1234)
        self.assertEqual(lib.entry(),C.addressof(entry))
        self.assertEqual(bytes(entry),original,'Live message attributes were modified')
        self.assertEqual(text.value,b'!!!ERROR!!! Message could not be loaded')
        return lib.text()

    def test_enabled_uses_short_literal_for_any_live_resource_offset(self):
        for region,lib in self.libs.items():
            for offset in (0,1,0x3e,0x400,0x1247,0x20000):
                expected={'JP':b'\x81\x49'*3,'PAL':b'!!!','US':b'!'}[region]
                self.assertEqual(self.invoke(lib,1,0,offset),expected,(region,offset))

    def test_off_passes_the_real_message_through(self):
        for lib in self.libs.values():
            self.assertEqual(self.invoke(lib,0,0,0x7e),b'!!!ERROR!!! Message could not be loaded')

    def test_stageloader_suppression_keeps_retail_dialogue(self):
        for lib in self.libs.values():
            self.assertEqual(self.invoke(lib,1,1,0x122),b'!!!ERROR!!! Message could not be loaded')

    def test_only_the_audited_fallback_call_is_replaced(self):
        source=(ROOT/'src/features.cpp').read_text()
        early=function(source,'featuresApplyEarly')
        self.assertIn('SUSAMUNE_MEM1_ADDR(0x8021530cu, 0x80153e1cu, 0x80148d9cu)',early)
        self.assertEqual(early.count('&setupFastTextBox'),1)
        self.assertNotIn('resolveFastTextPalMsg',source)
        self.assertNotIn('FHEAP',source)
        self.assertNotIn('FAST_TEXT_PAL_LANG_ADDR',source)


if __name__=='__main__':unittest.main()
