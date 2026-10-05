"""Exercise the embedded guide and its production controller-driven screen."""

import ctypes
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import gen_launcher_guide
from test_native_timer_creation import function

ROOT = Path(__file__).resolve().parents[1]
GUIDE = ROOT / "doc/launcher-guide-en.md"


class GuideContentTests(unittest.TestCase):
    def test_every_topic_fits_the_tv_viewport(self):
        topics = gen_launcher_guide.parse_guide(GUIDE.read_text(encoding="utf-8"))
        self.assertGreaterEqual(len(topics), 10)
        for title, lines in topics:
            with self.subTest(topic=title):
                self.assertLessEqual(len(title), 42)
                self.assertTrue(lines)
                self.assertTrue(all(len(line) <= 54 for line in lines))
                self.assertNotEqual(lines[0], "")
                self.assertNotEqual(lines[-1], "")

    def test_bad_sources_fail_before_packaging(self):
        for source in ("", "## Empty", "Text with no topic", "## Test\n\n" + "x"*55,
                       "## Test\n\nAn unsupported snowman: \u2603"):
            with self.subTest(source=source):
                with self.assertRaises(ValueError):
                    gen_launcher_guide.generate(source)

    def test_wrapping_preserves_paragraphs_and_numbered_steps(self):
        topics = gen_launcher_guide.parse_guide(
            "# Guide\n\n## Test\n\n1. " + "word "*15 + "\n\nNext paragraph.")
        lines = topics[0][1]
        self.assertTrue(lines[1].startswith("   "))
        self.assertEqual(lines[-2:], ["", "Next paragraph."])

    def test_launcher_entry_and_build_dependency_exist(self):
        source = (ROOT / "launcher/loader/source/SusamuneMenu.c").read_text()
        self.assertIn("case ROW_GUIDE:", function(source, "SusamuneMenuRun"))
        self.assertIn("GuideScreen();", function(source, "SusamuneMenuRun"))
        self.assertIn('"Guide%s"', function(source, "DrawMainMenu"))
        cmake = (ROOT / "launcher/loader/CMakeLists.txt").read_text()
        self.assertIn("gen_launcher_guide.py", cmake)
        self.assertIn("/doc/launcher-guide-en.md", cmake)
        self.assertIn('target_sources(loader PRIVATE "${_guide_data}")', cmake)

    def test_launcher_release_identity_is_consistent(self):
        menu = (ROOT / "launcher/loader/source/menu.c").read_text()
        build = function(menu, "PrintSusamuneBuild")
        guide = function((ROOT / "launcher/loader/source/SusamuneMenu.c").read_text(),
                         "GuideScreen")
        meta = (ROOT / "launcher/meta.xml.j2").read_text()
        self.assertIn('"Moonshine Launcher"', build)
        self.assertIn("<name>Moonshine Launcher</name>", meta)
        for source in (build, guide, meta):
            self.assertIn("V2.3.3 Frame By Frame", source)
            for old in ("FOXTROT", "PRE-RELEASE", "RC1"):
                self.assertNotIn(old, source.upper())


class GuideRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if sys.platform != "win32":
            raise unittest.SkipTest("Bundled Windows compiler required")
        cls.temp = tempfile.TemporaryDirectory(prefix="moonshine-guide-")
        cls.addClassCleanup(cls.temp.cleanup)
        work = Path(cls.temp.name)
        cls.topics = gen_launcher_guide.parse_guide(GUIDE.read_text(encoding="utf-8"))
        (work / "susamune_guide_data.inc").write_text(
            gen_launcher_guide.generate(GUIDE.read_text(encoding="utf-8")), encoding="ascii")
        menu = (ROOT / "launcher/loader/source/SusamuneMenu.c").read_text()
        source = r'''
#include "SusamuneGuide.h"
typedef unsigned u32; typedef int bool;
#define true 1
#define false 0
#define BLACK 0x000000ff
#define MENU_POS_X 25
#define MENU_POS_Y 34
#define DEFAULT_SIZE 16
#define ARROW_RIGHT ">"
typedef struct { u32 Up,Down,Left,Right; } HeldCounters;
static u32 Shutdown;
static int buttons[4096], buttonCount, frame, current, outOfBounds, terminated;
static int firstLines[4096],lastLines[4096],totals[4096],topicRows[4096];
static char titles[4096][128];
void *memset(void*p,int c,unsigned long long n){unsigned char*s=p;while(n--)*s++=c;return p;}
unsigned long long strlen(const char*s){unsigned n=0;while(s[n])n++;return n;}
int strcmp(const char*a,const char*b){while(*a&&*a==*b){a++;b++;}return *a-*b;}
void FPAD_Update(void){current=frame<buttonCount?buttons[frame]:32;frame++;}
int FPAD_Start(int lock){return 0;}
int FPAD_Cancel(int lock){return !!(current&32);}
int FPAD_OK(int lock){return !!(current&16);}
int FPAD_Up(int lock){return !!(current&1);}
int FPAD_Down(int lock){return !!(current&2);}
int FPAD_Left(int lock){return !!(current&4);}
int FPAD_Right(int lock){return !!(current&8);}
void LoaderShutdown(void){}
int SaveIfDirty(void){return 1;}
void ExitToLoader(int code){}
void ClearScreen(void){}
void GRRLIB_Render(void){}
void GRRLIB_Rectangle(int x,int y,int w,int h,u32 colour,int fill){
 if(x<25||y<25||x+w>615||y+h>455)outOfBounds++;
}
void PrintCenter(u32 colour,int y,const char*fmt,...){
 __builtin_va_list args; __builtin_va_start(args,fmt);
 if(!strcmp(fmt,"%s")){
  const char*s=__builtin_va_arg(args,const char*);int i=0;
  while(s[i]&&i<127){titles[frame-1][i]=s[i];i++;}titles[frame-1][i]=0;
 }else if(!strcmp(fmt,"%d-%d of %d lines")){
  firstLines[frame-1]=__builtin_va_arg(args,int);
  lastLines[frame-1]=__builtin_va_arg(args,int);
  totals[frame-1]=__builtin_va_arg(args,int);
 }
 __builtin_va_end(args);
 if(y<25||y+16>455)outOfBounds++;
}
void PrintFormat(int size,u32 colour,int x,int y,const char*fmt,...){
 __builtin_va_list args; __builtin_va_start(args,fmt);
 const char *text=!strcmp(fmt,"%s")?__builtin_va_arg(args,const char*):fmt;
 if(x<25||x+(int)strlen(text)*10>615||y<25||y+16>455)outOfBounds++;
 topicRows[frame-1]++;
 __builtin_va_end(args);
}
'''
        macro = menu[menu.index("#define FPAD_REPEAT(Key)"):menu.index("/** Devices **/")]
        source += macro + function(menu, "GuideScreen")
        source += r'''
__declspec(dllexport) int run(const int*input,int count){
 int i;memset(titles,0,sizeof(titles));memset(firstLines,0,sizeof(firstLines));
 memset(lastLines,0,sizeof(lastLines));memset(totals,0,sizeof(totals));
 memset(topicRows,0,sizeof(topicRows));frame=0;outOfBounds=0;buttonCount=count;
 for(i=0;i<count;i++)buttons[i]=input[i];GuideScreen();return frame;
}
__declspec(dllexport) int bounds(void){return outOfBounds;}
__declspec(dllexport) const char*title(int frame){return titles[frame];}
__declspec(dllexport) int first(int frame){return firstLines[frame];}
__declspec(dllexport) int last(int frame){return lastLines[frame];}
__declspec(dllexport) int total(int frame){return totals[frame];}
__declspec(dllexport) const char*line(int topic,int line){return SusamuneGuideLine(topic,line);}
__declspec(dllexport) int scroll(int first,int lines,int delta){return SusamuneGuideScroll(first,lines,delta);}
'''
        cfile = work / "guide.c"
        cfile.write_text(source, encoding="ascii")
        library = work / "guide.dll"
        subprocess.run([str(ROOT / "toolchain/clang.exe"), "--target=x86_64-pc-windows-msvc",
            "-shared", "-nostdlib", "-fno-builtin", "-fuse-ld=lld", "-Xlinker", "/noentry",
            "-I", str(ROOT / "launcher/loader/include"), "-I", str(work), str(cfile),
            str(ROOT / "launcher/loader/source/SusamuneGuide.c"), "-o", str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.addClassCleanup(lambda: ctypes.windll.kernel32.FreeLibrary(ctypes.c_void_p(cls.lib._handle)))
        cls.lib.line.restype = ctypes.c_char_p
        cls.lib.title.restype = ctypes.c_char_p

    def run_inputs(self, inputs):
        frames = self.lib.run((ctypes.c_int*len(inputs))(*inputs), len(inputs))
        self.assertLessEqual(frames, len(inputs) + 2)
        self.assertEqual(self.lib.bounds(), 0)
        return frames

    def test_all_embedded_lines_match_the_written_guide(self):
        for topic, (_, lines) in enumerate(self.topics):
            for number, text in enumerate(lines):
                self.assertEqual(self.lib.line(topic, number).decode(), text)
            self.assertEqual(self.lib.line(topic, -1), b"")
            self.assertEqual(self.lib.line(topic, len(lines)), b"")
        self.assertEqual(self.lib.line(-1, 0), b"")
        self.assertEqual(self.lib.line(len(self.topics), 0), b"")

    def test_open_read_scroll_and_return(self):
        self.run_inputs([0, 16, 0, 8, 0, 2, 0, 32, 0, 32])
        self.assertEqual(self.lib.title(1), b"Getting started")
        self.assertEqual(self.lib.first(1), 1)
        self.assertGreater(self.lib.first(3), 1)
        self.assertEqual(self.lib.total(7), 0)

    def test_topic_wrap_and_page_clamps(self):
        self.run_inputs([1, 0, 16, 0, 8, 0, 8, 0, 8, 0, 4, 0, 4, 0, 4, 0, 32, 0, 32])
        self.assertEqual(self.lib.title(2).decode(), self.topics[-1][0])
        self.assertEqual(self.lib.last(8), self.lib.total(8))
        self.assertEqual(self.lib.first(14), 1)

    def test_every_topic_can_be_read_to_its_end(self):
        inputs = []
        open_frames = []
        end_frames = []
        for _, lines in self.topics:
            open_frames.append(len(inputs))
            inputs.extend([16, 0])
            for _ in range((len(lines) + 13)//14):
                inputs.extend([8, 0])
            end_frames.append(len(inputs)-2)
            inputs.extend([32, 0, 2, 0])
        inputs.append(32)
        self.run_inputs(inputs)
        for i, (title, lines) in enumerate(self.topics):
            self.assertEqual(self.lib.title(open_frames[i]).decode(), title)
            self.assertEqual(self.lib.last(end_frames[i]), len(lines))

    def test_scroll_handles_short_topics_and_large_button_deltas(self):
        self.assertEqual(self.lib.scroll(0, 3, 14), 0)
        self.assertEqual(self.lib.scroll(0, 50, 2147483647), 36)
        self.assertEqual(self.lib.scroll(36, 50, -2147483648), 0)
        self.assertEqual(self.lib.scroll(100, 50, -1), 35)


if __name__ == "__main__":
    unittest.main()
