"""The real editor preview and live feedback share exactly the same text."""
import ctypes as C
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_native_timer_creation import function

ROOT = Path(__file__).resolve().parents[1]


class PracticeDisplayFormatTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / "toolchain/clang++.exe"
        if not compiler.exists():
            raise unittest.SkipTest("Bundled Windows compiler required")
        cls.temp = tempfile.TemporaryDirectory(prefix="moonshine-feedback-text-")
        cls.addClassCleanup(cls.temp.cleanup)
        work = Path(cls.temp.name)
        (work / "Dolphin").mkdir()
        (work / "Dolphin/types.h").write_text(
            "typedef unsigned char u8;typedef unsigned short u16;typedef unsigned u32;")
        source = (ROOT / "src/creation_extras.cpp").read_text()
        rows = dict(line.split("\t", 1) for line in
                    (ROOT / "data/japanese_ui.tsv").read_text(encoding="utf-8").splitlines()
                    if "\t" in line and not line.startswith("#"))
        cls.words = {key: rows[key] for key in ("Early", "On time", "Late", "Check jump", "Ready", "Waiting")}
        code = r'''
#define SUSAMUNE_VERSION_JP 1
#define private public
#include "susamune/creation_extras.hxx"
#include "susamune/glyphs.hxx"
#undef private
#define API extern "C" __declspec(dllexport)
extern "C" int _fltused=0;
void *memcpy(void*d,const void*s,unsigned long long n){for(unsigned long long i=0;i<n;i++)((char*)d)[i]=((const char*)s)[i];return d;}
bool same(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
bool japanese;
namespace JapaneseUi {
int (*format)(char*,unsigned long long,const char*,...);
const char *text(const char*key){if(!japanese)return key;
'''
        for english, japanese in cls.words.items():
            code += f"if(same(key,{json.dumps(english)}))return {json.dumps(japanese, ensure_ascii=False)};\n"
        code += r'''
return key;}}
namespace PackedText {const char*at(const char*p,int n){while(n--){while(*p)++p;++p;}return p;}}
char rendered[80],panel[80];unsigned drawnColor;
void copy(char*out,const char*in){unsigned i=0;while(in[i]){out[i]=in[i];++i;}out[i]=0;}
struct Menu{};
namespace Creation {void drawTextBox(Menu*,const CreationStyle&,const u8(*rgb)[3],u16,const char*text,bool,u16){
 copy(rendered,text);drawnColor=rgb[0][0];}}
void CreationEditor::draw(Menu*,const char*,const char*text)const{copy(panel,text);}
void CreationExtras::drawKeyboard(Menu*)const{}
void CreationExtras::drawSavestateFeedback(Menu*,const char*)const{}
void CreationExtras::drawWallkickDisplay(Menu*,const char*,int)const{}
void CreationExtras::drawRolloutDisplay(Menu*,const char*,int)const{}
void CreationExtras::drawDustDisplay(Menu*,const char*,int)const{}
void CreationExtras::drawToast(Menu*,const char*)const{}
void CreationExtras::drawPbBanner(Menu*,const char*)const{}
void CreationExtras::drawStageSessionCounter(Menu*,const char*)const{}
void CreationExtras::drawFailureBanner(Menu*,const char*,const char*)const{}
'''
        code += source[source.index("const char kWallkickNames"):source.index("inline int clampi")]
        code += "\n".join(function(source, name) for name in (
            "clampi", "formatPracticeDisplay", "drawMovementFeedback",
            "CreationExtras::drawPracticeDisplay", "CreationExtras::drawEditor"))
        code += r'''
CreationExtras extras;Menu menu;
API void init(void*formatter,unsigned language){
 JapaneseUi::format=(decltype(JapaneseUi::format))formatter;japanese=language!=0;
 extras.mKeyboard=false;extras.mEditor.mEditing=true;
 extras.mEditMode=CreationExtras::EDIT_PRACTICE_DISPLAY;
 for(unsigned d=0;d<3;++d)for(unsigned c=0;c<7;++c)extras.mPracticeDisplays[d].rgb[c][0]=c;
}
API const char*preview(unsigned display,unsigned color){
 extras.mEditFirst=display;extras.mEditor.mTextTarget=color+1;
 extras.drawEditor(&menu);return rendered;
}
API const char*editorText(){return panel;}
API unsigned color(){return drawnColor;}
API const char*live(unsigned display,unsigned color,unsigned frames,unsigned qf,float y,float v){
 char text[80];formatPracticeDisplay(text,sizeof(text),display,color,frames,qf,y,v);
 extras.drawPracticeDisplay(&menu,text,display,color);return rendered;
}
'''
        path = work / "fixture.cpp"
        path.write_text(code, encoding="utf-8")
        library = path.with_suffix(".dll")
        result = subprocess.run([str(compiler), "--target=x86_64-pc-windows-msvc", "-shared",
            "-nostdlib", "-fno-builtin", "-fuse-ld=lld", "-Wl,/noentry", "-O2",
            "-I", str(work), "-I", str(ROOT / "include"), str(path), "-o", str(library)],
            capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)
        cls.lib = C.CDLL(str(library))
        from _ctypes import FreeLibrary
        cls.addClassCleanup(FreeLibrary, cls.lib._handle)
        cls.crt = C.CDLL("msvcrt.dll")
        cls.lib.init.argtypes = [C.c_void_p, C.c_uint]
        cls.lib.preview.restype = cls.lib.editorText.restype = cls.lib.live.restype = C.c_char_p
        cls.lib.live.argtypes = [C.c_uint, C.c_uint, C.c_uint, C.c_uint, C.c_float, C.c_float]

    def initialize(self, japanese=False):
        self.lib.init(C.cast(self.crt._snprintf, C.c_void_p), japanese)

    def test_every_editor_target_matches_full_live_text_and_its_colour(self):
        for japanese in (False, True):
            self.initialize(japanese)
            word = lambda value: self.words[value] if japanese else value
            for display, count in enumerate((4, 7, 2)):
                for color in range(count):
                    with self.subTest(japanese=japanese, display=display, color=color):
                        frames = (8 if color == 0 else 10 if color == 2 else 9) if display == 0 else color + 1
                        y, v = (400, 5) if color == 3 else (404, 6)
                        live = self.lib.live(display, color, frames, 0, y, v)
                        preview = self.lib.preview(display, color)
                        self.assertEqual(preview, live)
                        self.assertEqual(self.lib.editorText(), live)
                        self.assertEqual(self.lib.color(), color)
                        if display == 0:
                            expected = f"{word(('Early','On time','Late','Check jump')[color])} {frames}f Y{y} V{v}.0"
                        elif display == 1:
                            expected = f"{word('Late') if color == 6 else str(frames)+'f'} qf0"
                        else:
                            expected = word("Ready" if color == 0 else "Waiting")
                        self.assertEqual(live.decode("utf-8"), expected)

    def test_live_keeps_actual_phase_and_gb_measurements_without_prefixes(self):
        self.initialize()
        self.assertEqual(self.lib.live(1, 0, 1, 3, 0, 0), b"1f qf3")
        self.assertEqual(self.lib.live(1, 6, 7, 2, 0, 0), b"Late qf2")
        self.assertEqual(self.lib.live(0, 2, 255, 0, 399, -4.5), b"Late 255+f Y399 V-4.5")


if __name__ == "__main__":
    unittest.main()
