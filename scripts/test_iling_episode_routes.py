"""Exercise episode destinations against the real IL catalogue and parent map."""
import ctypes as C
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_native_timer_creation import function

ROOT = Path(__file__).resolve().parents[1]


class EpisodeRouteTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / 'toolchain/clang++.exe'
        if not compiler.exists():
            raise unittest.SkipTest('Bundled Windows compiler required')
        cls.temp = tempfile.TemporaryDirectory(prefix='moonshine-episode-routes-')
        cls.addClassCleanup(cls.temp.cleanup)
        source = (ROOT / 'src/iling.cpp').read_text()
        warp = (ROOT / 'src/warp_wheel.cpp').read_text()
        sequence = (ROOT / 'include/SMS/System/GameSequence.hxx').read_text()
        code = '#include "susamune/iling_episodes.h"\n'
        code += 'typedef unsigned char u8;typedef unsigned int u32;\n'
        code += 'struct TGameSequence {' + sequence[sequence.index('    enum Area {'):sequence.index('    void set(')] + '};\n'
        code += 'namespace LevelWarp {struct Dest {enum {POST_CORONA=0x80};u8 area,episode,gameInt3;};u8 parentArea(u8);}\n'
        # Fast Any% origins have separate lifecycle coverage; these are the normal IL choices.
        code += 'namespace StageLoader {bool fastAnyStart(int){return false;}}\n'
        code += re.search(r'(?:constexpr|const) u8 kParentAreas\[\].*?\n};', warp, re.S).group(0)
        code += function(warp, 'LevelWarp::parentArea')
        code += source[source.index('enum FinishKind {'):source.index('const int kSecretOnlyPbSlotFirst')]
        code += 'struct Settings {int dirty;void markDirty(){++dirty;}}gSettings;\n'
        code += 'u8 sEpisodeChoices[SUSAMUNE_IL_EPISODE_COUNT];\n'
        code += 'bool sRunning;int sSelectedEntry;LevelWarp::Dest sAttemptStart;\n'
        for name in ('kEntryFullRedsFirst', 'kEntryFullRedsLast', 'kEntryNoki3Inside'):
            code += re.search(r'const int ' + name + r' = \d+;', source).group(0)
        for name in ('validEntry', 'pbSlot', 'episodeChoiceIndex', 'parentOrSelf',
                     'selectedStart', 'choosesPlazaState', 'selectedEpisode', 'setEpisode', 'entryFinish',
                     'isBonusShine', 'sameCourse', 'sameCourseEpisode', 'sameEpisodeShine'):
            code += function(source, name)
        code += r'''
#define API extern "C" __declspec(dllexport)
API int count(){return kEntryCount;}
API int slot(int entry){return pbSlot(entry);}
API int supported(int entry){return episodeChoiceIndex(entry)>=0;}
API unsigned original(int entry){auto d=kEntries[entry].start;return (d.area<<16)|(d.episode<<8)|d.gameInt3;}
API unsigned destination(int entry){auto d=selectedStart(entry);return (d.area<<16)|(d.episode<<8)|d.gameInt3;}
API int episode(int entry){return selectedEpisode(entry);}
API void choose(int entry,int episode){setEpisode(entry,episode);}
API void begin(int entry){sAttemptStart=selectedStart(entry);sSelectedEntry=entry;sRunning=true;}
API void end(){sRunning=false;}
API int accepts(int selected,int completed){return sameEpisodeShine(selected,completed);}
API int dirty(){return gSettings.dirty;}
API void reset(){for(auto&v:sEpisodeChoices)v=0;gSettings.dirty=0;sRunning=false;sSelectedEntry=-1;}
'''
        path = Path(cls.temp.name) / 'routes.cpp'
        path.write_text(code)
        proc = subprocess.run([str(compiler), '--target=x86_64-pc-windows-msvc', '-shared',
            '-nostdlib', '-fuse-ld=lld', '-Wl,/noentry', '-O2', '-I', str(ROOT/'include'),
            '-I', str(ROOT/'src'), str(path), '-o', str(path.with_suffix('.dll'))], capture_output=True, text=True)
        if proc.returncode:
            raise RuntimeError(proc.stdout + proc.stderr)
        cls.lib = C.CDLL(str(path.with_suffix('.dll')))
        cls.addClassCleanup(lambda:C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))

    def setUp(self):
        self.lib.reset()

    def test_twenty_episode_routes_and_gelato_plaza_state_have_choices(self):
        selected = [self.lib.slot(i) for i in range(self.lib.count()) if self.lib.supported(i)]
        self.assertEqual(set(selected), set(range(100,107)) | {29,59,69,112} | set(range(126,136)))
        self.assertEqual(len(selected),21)
        for i in range(self.lib.count()):
            self.assertEqual(self.lib.original(i),self.lib.destination(i))
            if not self.lib.supported(i):
                self.lib.choose(i,7)
                self.assertEqual(self.lib.original(i),self.lib.destination(i))

    def test_all_choices_preserve_pb_identity_and_original_internal_start(self):
        parents = {7:6,13:5}
        for i in range(self.lib.count()):
            if not self.lib.supported(i):
                continue
            original,slot = self.lib.original(i),self.lib.slot(i)
            area,default = original >> 16,original & 255
            for episode in range(8):
                self.lib.choose(i,episode)
                expected = original if episode==default else (parents.get(area,area)<<16)|(episode<<8)|episode
                if slot == 112:
                    expected = (1 << 16) | ([0,1,5,2,7,8,9,2][episode] << 8) | 9 | (0x80 if episode == 7 else 0)
                self.assertEqual(self.lib.destination(i),expected,(i,slot,episode))
                self.assertEqual(self.lib.episode(i),episode)
                self.assertEqual(self.lib.slot(i),slot)
        self.assertEqual(self.lib.dirty(),168)

    def test_bad_indices_and_episode_values_leave_choices_untouched(self):
        for entry in (-1,self.lib.count(),999):
            self.lib.choose(entry,0)
            self.assertEqual(self.lib.supported(entry),0)
            self.assertEqual(self.lib.episode(entry),-1)
        for i in range(self.lib.count()):
            for invalid in (-1,8,255):
                self.lib.choose(i,invalid)
            self.assertEqual(self.lib.destination(i),self.lib.original(i))
        self.assertEqual(self.lib.dirty(),0)

    def test_streak_bonus_finish_uses_running_start_after_next_episode_is_edited(self):
        by_slot = {self.lib.slot(i): i for i in range(self.lib.count())}
        for bonus, first, second in ((100, 0, 3), (29, 20, 21),
                                     (59, 50, 51), (69, 60, 61)):
            selected = by_slot[bonus]
            next_episode = self.lib.original(by_slot[second]) & 255
            self.lib.choose(selected, 0)
            self.lib.begin(selected)
            self.lib.choose(selected, next_episode)
            self.assertEqual(self.lib.accepts(selected, by_slot[first]), 1, bonus)
            self.assertEqual(self.lib.accepts(selected, by_slot[second]), 0, bonus)
            self.assertEqual(self.lib.episode(selected), next_episode)
            self.assertEqual(self.lib.slot(selected), bonus)
            self.lib.end()
            self.assertEqual(self.lib.accepts(selected, by_slot[first]), 0, bonus)
            self.assertEqual(self.lib.accepts(selected, by_slot[second]), 1, bonus)

    def test_full_reds_always_requires_its_exact_completed_entry(self):
        for selected in range(self.lib.count()):
            if not 126 <= self.lib.slot(selected) <= 135:
                continue
            self.lib.begin(selected)
            self.lib.choose(selected, 7)
            for completed in range(self.lib.count()):
                self.assertEqual(self.lib.accepts(selected, completed),
                                 int(selected == completed), (selected, completed))
            self.lib.end()


if __name__=='__main__':unittest.main()
