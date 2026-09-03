"""Contracts for transient launcher-music reuse of the snapshot window."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class LauncherMusicMem2Contracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = (
            ROOT / "launcher" / "loader" / "source" / "SusamuneMusic.c"
        ).read_text()
        cls.mem2 = (
            ROOT / "include" / "susamune" / "mem2_map.h"
        ).read_text()
        cls.main = (
            ROOT / "launcher" / "loader" / "source" / "main.c"
        ).read_text()

    def test_music_uses_no_system_heap(self) -> None:
        self.assertIn("SUSAMUNE_LAUNCHER_MUSIC_PPC_BASE", self.source)
        self.assertNotIn("memalign(", self.source)
        self.assertNotIn("free(", self.source)
        self.assertNotIn("static void *sBuffer", self.source)
        self.assertNotIn("sBufferSpan", self.source)

    def test_overlay_is_bounded_by_snapshot_window(self) -> None:
        values = {}
        for name in ("SUSAMUNE_MEM2_SNAPSHOT_SIZE",
                     "SUSAMUNE_LAUNCHER_MUSIC_SIZE"):
            match = re.search(r"#define\s+{}\s+(0x[0-9A-Fa-f]+)u".format(name),
                              self.mem2)
            self.assertIsNotNone(match, name)
            values[name] = int(match.group(1), 16)
        self.assertEqual(values["SUSAMUNE_LAUNCHER_MUSIC_SIZE"], 4 << 20)
        self.assertLessEqual(values["SUSAMUNE_LAUNCHER_MUSIC_SIZE"],
                             values["SUSAMUNE_MEM2_SNAPSHOT_SIZE"])
        self.assertIn("Launcher music overlay must fit", self.mem2)

    def test_cache_ownership_is_relinquished_before_gameplay(self) -> None:
        read = self.source.index("f_read(&file, MUSIC_BUFFER")
        publish = self.source.index("DCFlushRange(MUSIC_BUFFER, allocationSize)",
                                    read)
        validate = self.source.index("SusamuneMp3Validate", publish)
        self.assertLess(read, publish)
        self.assertLess(publish, validate)
        shutdown = self.source.index("void SusamuneMusicShutdown(void)")
        stop = self.source.index("MP3Player_Stop();", shutdown)
        release = self.source.index("ReleaseMusicBuffer(", stop)
        self.assertLess(stop, release)

        shutdown = self.main.index("SusamuneMusicShutdown();")
        game_handoff = self.main.index('gprintf("GameRegion:")', shutdown)
        self.assertLess(shutdown, game_handoff)


if __name__ == "__main__":
    unittest.main()
