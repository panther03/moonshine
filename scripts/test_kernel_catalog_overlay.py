#!/usr/bin/env python3
"""Source invariants for the ARM ghost catalog lifetime overlay."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
KERNEL = ROOT / "launcher/kernel/SusamuneGhost.c"


def source_function(name: str) -> str:
    source = KERNEL.read_text(encoding="utf-8")
    match = re.search(rf"static\s+void\s+{name}\s*\(", source)
    if match is None:
        raise AssertionError(f"could not find {name}")
    begin = source.index("{", match.end())
    depth = 0
    for end in range(begin, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():end + 1]
    raise AssertionError(f"unterminated {name}")


class GhostCatalogOverlayTests(unittest.TestCase):
    def test_catalog_and_import_work_share_one_checked_union(self) -> None:
        source = KERNEL.read_text(encoding="utf-8")
        self.assertIn("union GhostCatalogStorage", source)
        self.assertIn(
            "struct SlotCatalog personal[SUSAMUNE_GHOST_SLOT_COUNT]",
            source,
        )
        self.assertIn(
            "struct ImportedSlot catalog["
            "SUSAMUNE_GHOST_IMPORTED_MAX_ENTRIES]",
            source,
        )
        for member in (
            "struct ImportedSlot candidate;",
            "DIR dir;",
            "FILINFO entry;",
            "typedef char ImportedCatalogWorkFitsPersonal[",
        ):
            self.assertIn(member, source)
        self.assertNotIn("static struct SlotCatalog Catalog[", source)
        self.assertNotIn("static struct ImportedSlot ImportedCatalog[", source)
        self.assertNotIn("static struct ImportedSlot ImportCandidate;", source)
        self.assertNotIn("static DIR ImportDir;", source)
        self.assertNotIn("static FILINFO ImportEntry;", source)

    def test_scan_switch_invalidates_before_overwriting_union(self) -> None:
        personal = source_function("BeginCatalogScan")
        self.assertLess(
            personal.index("ImportedCatalogReady = false;"),
            personal.index("memset(Catalog, 0, sizeof(Catalog));"),
        )
        imported = source_function("BeginImportScan")
        self.assertLess(
            imported.index("CatalogReady = false;"),
            imported.index(
                "memset(ImportedCatalog, 0, sizeof(ImportedCatalog));"
            ),
        )

    def test_import_scan_errors_close_the_overlaid_directory(self) -> None:
        for function in (
            "ImportScanFileOpenPass",
            "ImportScanFileClosePass",
        ):
            with self.subTest(function=function):
                source = source_function(function)
                self.assertIn("FinishIoError(closeRet);", source)
                self.assertNotIn(
                    "FinishRequest(StorageStatusForResult(closeRet)", source
                )


if __name__ == "__main__":
    unittest.main()
