import hashlib
import re
import struct
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


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
        self.assertIn("/* #define TT_CONFIG_OPTION_UNPATENTED_HINTING */",
                      options)
        self.assertNotRegex(
            options, r"(?m)^\s*#define TT_CONFIG_OPTION_UNPATENTED_HINTING$")
        self.assertEqual(
            hashlib.sha256(archive_bytes).hexdigest().upper(),
            "250649849EA117286D8A8F4D64448DB403D6C0C74297E637CE3E48BC7832678C")
        self.assertIn(b"GCC: (devkitPPC release 35) 8.3.0", archive_bytes)

        audit = (ROOT / "doc" / "v2.2.0-freetype-audit.md").read_text(
            encoding="utf-8")
        self.assertIn("VER-2-4-12", audit)
        self.assertIn("cecf93ef90c660a8b0b45e5adbbfb5ea443fb6b9", audit)

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
