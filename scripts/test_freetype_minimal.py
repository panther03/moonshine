import hashlib
import re
import struct
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def _archive_member(archive: bytes, wanted: str) -> bytes:
    """Return one member from a GNU ar archive without external tools."""
    if not archive.startswith(b"!<arch>\n"):
        raise AssertionError("invalid FreeType archive")
    offset = 8
    long_names = b""
    while offset < len(archive):
        header = archive[offset:offset + 60]
        if len(header) != 60 or header[58:60] != b"`\n":
            raise AssertionError("invalid FreeType archive member")
        size = int(header[48:58].decode("ascii").strip())
        data_start = offset + 60
        data = archive[data_start:data_start + size]
        raw_name = header[:16].decode("ascii").strip()
        if raw_name == "//":
            long_names = data
        elif raw_name.startswith("/") and raw_name[1:].isdigit():
            name_start = int(raw_name[1:])
            name_end = long_names.find(b"/\n", name_start)
            if name_end < 0:
                raise AssertionError("invalid GNU ar name table")
            name = long_names[name_start:name_end].decode("ascii")
            if name == wanted:
                return data
        elif raw_name.rstrip("/") == wanted:
            return data
        offset = data_start + size + (size & 1)
    raise AssertionError(f"missing archive member {wanted}")


def _elf_section(elf: bytes, wanted: str) -> bytes:
    """Return one section from a big-endian ELF32 object."""
    if elf[:6] != b"\x7fELF\x01\x02":
        raise AssertionError("invalid PowerPC ELF member")
    header = struct.unpack_from(">HHIIIIIHHHHHH", elf, 16)
    section_offset = header[5]
    section_entry_size = header[10]
    section_count = header[11]
    name_index = header[12]
    sections = [
        struct.unpack_from(">IIIIIIIIII", elf,
                           section_offset + index * section_entry_size)
        for index in range(section_count)
    ]
    names_header = sections[name_index]
    names = elf[names_header[4]:names_header[4] + names_header[5]]
    for section in sections:
        end = names.find(b"\0", section[0])
        name = names[section[0]:end].decode("ascii")
        if name == wanted:
            return elf[section[4]:section[4] + section[5]]
    raise AssertionError(f"missing ELF section {wanted}")


class MinimalFreeTypeTests(unittest.TestCase):
    def test_embedded_font_is_unicode_truetype_outline(self) -> None:
        archive = ROOT / "launcher" / "loader" / "data" / "font.zip"
        with zipfile.ZipFile(archive) as zf:
            self.assertEqual(zf.namelist(), ["font.ttf"])
            font = zf.read("font.ttf")

        self.assertEqual(font[:4], b"\x00\x01\x00\x00")
        table_count = struct.unpack_from(">H", font, 4)[0]
        tables = {}
        for index in range(table_count):
            tag, _, offset, size = struct.unpack_from(">4sIII", font, 12 + 16 * index)
            self.assertLessEqual(offset + size, len(font))
            tables[tag] = font[offset:offset + size]

        self.assertTrue({b"cmap", b"glyf", b"head", b"hhea", b"hmtx",
                         b"loca", b"maxp", b"post"}.issubset(tables))
        self.assertNotIn(b"CFF ", tables)
        self.assertEqual(struct.unpack_from(">I", tables[b"post"], 0)[0],
                         0x00030000)

        cmap = tables[b"cmap"]
        self.assertEqual(struct.unpack_from(">H", cmap, 0)[0], 0)
        encoding_count = struct.unpack_from(">H", cmap, 2)[0]
        encodings = {
            struct.unpack_from(">HH", cmap, 4 + 8 * index)
            for index in range(encoding_count)
        }
        self.assertTrue(any(platform == 0 or
                            (platform == 3 and encoding in (1, 10))
                            for platform, encoding in encodings))

    def test_allowlist_keeps_only_rendering_pipeline(self) -> None:
        source = (ROOT / "launcher" / "loader" / "source" /
                  "SusamuneFreeType.c").read_text(encoding="utf-8")
        match = re.search(r"modules\[\]\s*=\s*\{(.*?)\};", source, re.DOTALL)
        self.assertIsNotNone(match)
        modules = re.findall(r"&(\w+)", match.group(1))
        self.assertEqual(modules, [
            "tt_driver_class",
            "sfnt_module_class",
            "autofit_module_class",
            "ft_raster1_renderer_class",
            "ft_smooth_renderer_class",
        ])

        launcher_sources = "\n".join(
            path.read_text(encoding="utf-8", errors="replace")
            for path in (ROOT / "launcher" / "loader" / "source").rglob("*.c")
        )
        self.assertNotIn("FT_Get_Glyph_Name(", launcher_sources)
        self.assertNotIn("FT_Get_Postscript_Name(", launcher_sources)
        self.assertNotIn("FT_RENDER_MODE_LCD", launcher_sources)
        self.assertNotIn("FT_LOAD_TARGET_LCD", launcher_sources)

    def test_vendored_freetype_abi_and_hinting_are_pinned(self) -> None:
        include = ROOT / "launcher" / "loader" / "extlibs" / "include" / "freetype"
        archive = (ROOT / "launcher" / "loader" / "extlibs" / "lib" /
                   "libfreetype.a")
        public = (include / "freetype.h").read_text(encoding="utf-8")
        options = (include / "config" / "ftoption.h").read_text(encoding="utf-8")
        archive_bytes = archive.read_bytes()

        self.assertRegex(public, r"#define FREETYPE_MAJOR\s+2")
        self.assertRegex(public, r"#define FREETYPE_MINOR\s+4")
        self.assertRegex(public, r"#define FREETYPE_PATCH\s+12")
        self.assertIn("/* #define TT_CONFIG_OPTION_BYTECODE_INTERPRETER */", options)
        self.assertRegex(
            options, r"(?m)^\s*#define TT_CONFIG_OPTION_UNPATENTED_HINTING$")
        self.assertEqual(
            hashlib.sha256(archive_bytes).hexdigest().upper(),
            "0A99B683903BBA63C325EA865FB3279B1315DD2A0B488F1FC4E2C49A1B6F7E70")
        self.assertIn(b"GCC: (devkitPPC release 35) 8.3.0", archive_bytes)

        audit = (ROOT / "doc" / "v2.2.0-freetype-audit.md").read_text(
            encoding="utf-8")
        self.assertIn("VER-2-4-12", audit)
        self.assertIn("cecf93ef90c660a8b0b45e5adbbfb5ea443fb6b9", audit)

    def test_truetype_face_layout_matches_the_legacy_sfnt_member(self) -> None:
        archive = (ROOT / "launcher" / "loader" / "extlibs" / "lib" /
                   "libfreetype.a").read_bytes()
        truetype = _archive_member(archive, "truetype.o")
        driver = _elf_section(truetype, ".rodata.tt_driver_class")

        # sfnt.o was built with the legacy hinter field and reads the
        # postscript_name pointer at +0x298.  A 0x310 face is four bytes short
        # and makes that cleanup path free glyf_len instead.
        self.assertEqual(struct.unpack_from(">I", driver, 0x24)[0], 0x314)

    def test_superbuild_enables_and_forwards_allowlist(self) -> None:
        top = (ROOT / "launcher" / "CMakeLists.txt").read_text(encoding="utf-8")
        loader = (ROOT / "launcher" / "loader" / "CMakeLists.txt").read_text(
            encoding="utf-8")

        option = re.compile(
            r"option\(SUSAMUNE_MINIMAL_FREETYPE\s+"
            r'"[^"]+"\s+ON\)', re.DOTALL)
        self.assertRegex(top, option)
        self.assertRegex(loader, option)
        self.assertIn(
            '"-DSUSAMUNE_MINIMAL_FREETYPE=${SUSAMUNE_MINIMAL_FREETYPE}"',
            top)


if __name__ == "__main__":
    unittest.main()
