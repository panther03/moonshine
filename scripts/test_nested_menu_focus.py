"""Exercise production page-entry methods with the real raw-button guard."""

from pathlib import Path
import ctypes
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for end in range(brace, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[start:end + 1]
    raise AssertionError(signature)


class NestedMenuFocusTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / "toolchain/clang++.exe"
        if sys.platform != "win32" or not compiler.exists():
            raise unittest.SkipTest("bundled Windows host compiler unavailable")
        source = (ROOT / "src/menu.cpp").read_text(encoding="utf-8")
        source = source[source.index("class NestedMenuTab final"):]
        methods = "\n".join(function(source, signature) for signature in (
            "bool suppressesBinds() const override",
            "void focus() override",
            "bool beginProtectedPBSave(Menu *menu, u32 token) override",
            "void update(Menu *menu, TMarioGamePad *pad) override",
        ))
        raw = (ROOT / "include/susamune/raw_prompt_input.hxx").read_text()
        raw = raw[raw.index("class RawPromptInput"):raw.index("#endif")]
        shim = r'''
typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef unsigned u32;
struct JUTGamePad {
    enum { A=0x100, B=0x200 };
    struct Status { u16 mButton; };
    static Status mPadStatus[1];
};
JUTGamePad::Status JUTGamePad::mPadStatus[1];
struct TMarioGamePad {
    enum { CSTICK_UP=1, CSTICK_DOWN=2, CSTICK_LEFT=4, CSTICK_RIGHT=8 };
    u32 rapid;
};
struct Menu { u32 navigationInput(TMarioGamePad *) { return 0; } void toast(const char *) {} };
int wrap(int value, int count) { return (value + count) % count; }
struct MenuTab {
    virtual void focus() {}
    virtual bool beginProtectedPBSave(Menu *, u32) { return false; }
    virtual bool grabsInput() const { return false; }
    virtual bool suppressesBinds() const { return false; }
    virtual bool back() { return false; }
    virtual bool available() const { return true; }
    virtual void update(Menu *, TMarioGamePad *) {}
};
'''
        router = r'''
class NestedMenuTab final : public MenuTab {
public:
    NestedMenuTab(MenuTab *child) : mCount(1),mSel(0),mPage(-1),mChildEntryWait(false) {
        mChildren[0]=child; mNavInput.begin(JUTGamePad::A);
    }
    MenuTab *current() const { return mPage < 0 ? 0 : mChildren[mPage]; }
    void jumpRootSection(int) {}
METHODS
private:
    MenuTab *mChildren[1];
    u8 mCount,mSel;
    s8 mPage;
    bool mChildEntryWait;
    RawPromptInput mNavInput;
};
struct Child : MenuTab {
    RawPromptInput input;
    unsigned actions=0, focuses=0;
    bool enabled=true;
    Child() { input.begin(JUTGamePad::A); }
    void focus() override { ++focuses; input.begin(JUTGamePad::A); }
    bool available() const override { return enabled; }
    bool beginProtectedPBSave(Menu *, u32) override { return true; }
    void update(Menu *, TMarioGamePad *) override {
        if (input.update() & JUTGamePad::A) ++actions;
    }
};
struct DecodedChild : MenuTab {
    unsigned actions=0;
    void update(Menu *, TMarioGamePad *pad) override {
        if (pad->rapid & JUTGamePad::A) ++actions;
    }
};
extern "C" __declspec(dllexport) unsigned entryGuardBinds(unsigned held) {
    JUTGamePad::mPadStatus[0].mButton=0;
    Menu menu; TMarioGamePad pad; DecodedChild child; NestedMenuTab router(&child);
    JUTGamePad::mPadStatus[0].mButton=JUTGamePad::A;
    router.update(&menu,&pad);
    JUTGamePad::mPadStatus[0].mButton=(u16)held;
    return router.suppressesBinds();
}
extern "C" __declspec(dllexport) unsigned decodedEntry(unsigned backButton) {
    JUTGamePad::mPadStatus[0].mButton=0;
    Menu menu; TMarioGamePad pad; DecodedChild child; NestedMenuTab router(&child);
    JUTGamePad::mPadStatus[0].mButton=JUTGamePad::A;
    pad.rapid=JUTGamePad::A;
    router.update(&menu,&pad);
    for (unsigned i=0;i<20;++i) router.update(&menu,&pad);
    unsigned before=child.actions;
    unsigned suppressed=router.suppressesBinds() ? 0x1000000 : 0;
    JUTGamePad::mPadStatus[0].mButton=0;
    // The release callback still carries the old decoded trigger.
    router.update(&menu,&pad);
    unsigned releaseActions=child.actions;
    pad.rapid=0;
    router.update(&menu,&pad);
    JUTGamePad::mPadStatus[0].mButton=backButton ? JUTGamePad::B : JUTGamePad::A;
    pad.rapid=JUTGamePad::A;
    router.update(&menu,&pad);
    return before | (child.actions<<8) | (router.current() ? 0x10000 : 0) |
           (releaseActions<<20) | suppressed;
}
extern "C" __declspec(dllexport) unsigned exercise(unsigned scenario) {
    JUTGamePad::mPadStatus[0].mButton=0;
    Menu menu; TMarioGamePad pad; Child child; NestedMenuTab router(&child);
    if (scenario==3) child.enabled=false;
    JUTGamePad::mPadStatus[0].mButton=JUTGamePad::A;
    if (scenario==2) router.beginProtectedPBSave(&menu,1);
    else router.update(&menu,&pad);
    if (scenario==1) router.focus();
    for (unsigned i=0;i<20;++i) router.update(&menu,&pad);
    unsigned before=child.actions;
    JUTGamePad::mPadStatus[0].mButton=0;
    router.update(&menu,&pad);
    JUTGamePad::mPadStatus[0].mButton=JUTGamePad::A;
    router.update(&menu,&pad);
    return before | (child.actions<<8) | (child.focuses<<16) |
           (router.current() ? 0x1000000 : 0);
}
'''.replace("METHODS", methods)
        cls.folder = tempfile.TemporaryDirectory()
        path = Path(cls.folder.name) / "nested.cpp"
        path.write_text(shim + raw + router)
        library = path.with_suffix(".dll")
        subprocess.run([str(compiler), "--target=x86_64-pc-windows-msvc",
                        "-shared", "-nostdlib", "-fuse-ld=lld", "-Wl,/noentry",
                        "-fno-rtti", "-fno-exceptions", str(path), "-o", str(library)],
                       check=True)
        cls.library = ctypes.CDLL(str(library))

    @classmethod
    def tearDownClass(cls):
        ctypes.windll.kernel32.FreeLibrary(ctypes.c_void_p(cls.library._handle))
        cls.folder.cleanup()

    def test_held_entry_a_waits_for_release_and_a_fresh_press(self):
        self.assertEqual(self.library.exercise(0), 0x1010100)

    def test_refocusing_an_active_page_refreshes_its_guard(self):
        self.assertEqual(self.library.exercise(1), 0x1010100)

    def test_custom_protected_entry_also_focuses_the_child(self):
        self.assertEqual(self.library.exercise(2), 0x1010100)

    def test_removed_rng_gate_no_longer_hides_a_page(self):
        self.assertEqual(self.library.exercise(3), 0x1010100)

    def test_decoded_child_without_focus_skips_the_stale_release_callback(self):
        self.assertEqual(self.library.decodedEntry(0), 0x1010100)

    def test_fresh_back_still_exits_without_activating_the_child(self):
        self.assertEqual(self.library.decodedEntry(1), 0x1000000)

    def test_entry_guard_owns_binds_for_either_select_or_back(self):
        for held in (0x100, 0x200, 0x300):
            self.assertEqual(self.library.entryGuardBinds(held), 1)
        self.assertEqual(self.library.entryGuardBinds(0), 0)


if __name__ == "__main__":
    unittest.main()
