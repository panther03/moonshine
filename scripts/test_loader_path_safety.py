"""Contracts for bounded legacy-loader paths and Triforce dispatch."""

from __future__ import annotations

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
TRI_SOURCE = ROOT / "launcher" / "loader" / "source" / "TRI.c"
MOD_SOURCE = ROOT / "launcher" / "loader" / "source" / "SusamuneMod.c"
TITLES_SOURCE = ROOT / "launcher" / "loader" / "source" / "titles.c"
MD5_SOURCE = ROOT / "launcher" / "loader" / "source" / "md5_db.c"
UPDATE_SOURCE = ROOT / "launcher" / "loader" / "source" / "update.c"


EXPECTED_TRIFORCE_GAMES = [
    (0x210320, "Mario Kart Arcade GP (ENG Feb 14 2006 13:09:48)",
     "CARD_NAME_GP1", 0x45, False),
    (0x25C0AC, "Mario Kart Arcade GP 2 (ENG Feb 7 2007 02:47:24)",
     "CARD_NAME_GP2", 0x45, False),
    (0x25C664, "Mario Kart Arcade GP 2 (JPN Feb 6 2007 20:29:25)",
     "CARD_NAME_GP2J", 0x45, False),
    (0x181E60, "F-Zero AX (Rev C)",
     "SETTINGS_AX_RVC", 0x2A, True),
    (0x1821C4, "F-Zero AX (Rev D)",
     "SETTINGS_AX_RVD", 0x2A, True),
    (0x18275C, "F-Zero AX (Rev E)",
     "SETTINGS_AX_RVE", 0x2A, True),
    (0x01C2DF4, "Virtua Striker 3 Ver 2002",
     "SETTINGS_VS3V02", 0x12, False),
    (0x01CF1C4, "Virtua Striker 4 (Japan)",
     "SETTINGS_VS4JAP", 0x2B, False),
    (0x1C51E4, "Virtua Striker 4 (Export) (GDT-0014)",
     "SETTINGS_VS4EXP", 0x2B, False),
    (0x1C5514, "Virtua Striker 4 (Export) (GDT-0015)",
     "SETTINGS_VS4EXP", 0x2B, False),
    (0x24A4C8, "Virtua Striker 4 Ver 2006 (Japan) (Rev B)",
     "SETTINGS_VS4V06JAP", 0x2E, False),
    (0x24B248, "Virtua Striker 4 Ver 2006 (Japan) (Rev D)",
     "SETTINGS_VS4V06JAP", 0x2E, False),
    (0x20D7E8, "Virtua Striker 4 Ver 2006 (Export)",
     "SETTINGS_VS4V06EXP", 0x2B, False),
    (0x26B3F4, "Gekitou Pro Yakyuu (Rev B)",
     "SETTINGS_YAKRVB", 0xF5, False),
    (0x26D9B4, "Gekitou Pro Yakyuu (Rev C)",
     "SETTINGS_YAKRVC", 0x100, False),
]


def parse_triforce_games(source: str) -> list[tuple[int, str, str, int, bool]]:
    start = source.index("static const TriforceGame TriforceGames[]")
    end = source.index("\n};", start)
    block = source[start:end]
    pattern = re.compile(
        r'\{(0x[0-9A-Fa-f]+),\s*"([^"]+)",\s*'
        r'([A-Z0-9_]+),\s*(0x[0-9A-Fa-f]+|0),\s*'
        r'(true|false)\}',
        re.DOTALL,
    )
    return [
        (int(offset, 0), name, save, int(size, 0), needs_ax == "true")
        for offset, name, save, size, needs_ax in pattern.findall(block)
    ]


def parse_titles(data: bytes) -> list[bytes]:
    """Host oracle for the byte-level bounded parser in titles.c."""
    titles: list[bytes] = []
    line = bytearray()
    discard = False
    for value in data + b"\0":
        if value == ord("\r"):
            continue
        if value in (0, ord("\n")):
            if not discard and len(line) > 5:
                titles.append(bytes(line))
                if len(titles) == 740:
                    break
            line.clear()
            discard = False
            if value == 0:
                break
        elif not discard:
            if len(line) == 63:
                discard = True
            else:
                line.append(value)
    return titles


class LoaderPathSafetyContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tri = TRI_SOURCE.read_text()
        cls.mod = MOD_SOURCE.read_text()
        cls.titles = TITLES_SOURCE.read_text()
        cls.md5 = MD5_SOURCE.read_text()
        cls.update = UPDATE_SOURCE.read_text()

    def test_triforce_table_preserves_all_ordered_actions(self) -> None:
        self.assertEqual(parse_triforce_games(self.tri), EXPECTED_TRIFORCE_GAMES)
        loop = self.tri[self.tri.index("for (i = 0;"):]
        self.assertIn("DOLRead32(game->dolOffset", loop)
        self.assertIn("CreateTriforceFile(game->savePath, game->saveSize);", loop)
        self.assertIn("CreateTriforceFile(CARD_NAME_AX, 0xCF);", loop)
        self.assertLess(loop.index("CreateTriforceFile(CARD_NAME_AX"),
                        loop.index("CreateTriforceFile(game->savePath"))

    def test_all_triforce_save_paths_fit_the_bounded_buffer(self) -> None:
        constants = dict(re.findall(
            r'static const char ([A-Z0-9_]+)\[\] = "([^"]+)";', self.tri
        ))
        referenced = {
            symbol
            for entry in EXPECTED_TRIFORCE_GAMES
            for symbol in entry[2:3]
        }
        referenced.add("CARD_NAME_AX")
        self.assertEqual(referenced, set(constants))
        for symbol in referenced:
            self.assertLessEqual(len("usb:" + constants[symbol]) + 1, 64)

    def test_live_paths_fail_closed_on_snprintf_truncation(self) -> None:
        self.assertEqual(self.tri.count("(unsigned int)written >= sizeof("), 3)
        self.assertIn("read != 4", self.tri)
        self.assertIn('"%s" SUSAMUNE_MOD_FILE_FMT', self.mod)
        self.assertIn("(unsigned int)written >= sizeof(path)", self.mod)
        self.assertNotIn("char name[32]", self.mod)
        self.assertIn("(unsigned int)written >= sizeof(filepath)", self.titles)
        self.assertIn("(unsigned int)written >= sizeof(filepath)", self.md5)
        self.assertIn("(unsigned int)snprintf(filepath, sizeof(filepath)",
                      self.update)
        self.assertIn("Error opening '%.19s'", self.update)

    def test_wiivc_disc_reads_fail_closed(self) -> None:
        self.assertIn("if (WDVD_FST_OpenDisc(0) != 0)", self.tri)
        self.assertEqual(self.tri.count("WDVD_FST_Read(wdvdTmpBuf, 4) != 4"), 2)
        setup = self.tri[self.tri.index("u32 TRISetupGames("):]
        initial_read = setup[setup.index("wiiVCInternal"):]
        short_read = initial_read.index("WDVD_FST_Read(wdvdTmpBuf, 4) != 4")
        close = initial_read.index("WDVD_FST_Close();", short_read)
        failure_return = initial_read.index("return 0;", short_read)
        self.assertLess(close, failure_return)

    def test_titles_capacity_and_malformed_line_boundaries(self) -> None:
        rows = [f"{i:03X}-Title {i}".encode() for i in range(741)]
        self.assertEqual(len(parse_titles(b"\n".join(rows))), 740)
        self.assertEqual(parse_titles(b"ABC-" + b"x" * 59 + b"\nDEF-Next"),
                         [b"ABC-" + b"x" * 59, b"DEF-Next"])
        self.assertEqual(parse_titles(b"ABC-" + b"x" * 60 + b"\nDEF-Next"),
                         [b"DEF-Next"])
        self.assertEqual(parse_titles(b"short\r\nABC-Last"), [b"ABC-Last"])

    def test_titles_source_never_forms_a_one_past_slot(self) -> None:
        load = self.titles[self.titles.index("int LoadTitles(void)"):]
        self.assertNotIn("cur_title", load)
        self.assertIn("if (title_count == MAX_TITLES)", load)
        self.assertIn("if (pos == (LINE_LENGTH - 1))", load)
        self.assertIn("discard = true;", load)


if __name__ == "__main__":
    unittest.main()
