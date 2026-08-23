#!/usr/bin/env python3
"""Build the bounded launcher font archive from the upstream full font."""

from __future__ import annotations

import argparse
import io
from pathlib import Path
import tempfile
import unicodedata
import zipfile

try:
    from fontTools import subset
    from fontTools.ttLib import TTFont
except ImportError as exc:  # pragma: no cover - optional release tool
    raise SystemExit("fonttools is required: python -m pip install fonttools") from exc


REQUIRED = (*range(0x20, 0x7F), 0x25C0, 0x25B6, 0xFFFD)


def _font_data(path: Path) -> bytes:
    if path.suffix.lower() == ".ttf":
        return path.read_bytes()
    with zipfile.ZipFile(path) as archive:
        fonts = [name for name in archive.namelist() if name.lower().endswith(".ttf")]
        if len(fonts) != 1:
            raise ValueError(f"{path} must contain exactly one TTF")
        return archive.read(fonts[0])


def _keep_codepoint(codepoint: int) -> bool:
    category = unicodedata.category(chr(codepoint))
    return (
        category[0] in "LMNP"
        or category in {"Sc", "Zs"}
        or 0x2190 <= codepoint <= 0x21FF
        or 0x25A0 <= codepoint <= 0x25FF
        or codepoint in REQUIRED
    )


def _selected_codepoints(cmap: dict[int, str]) -> list[int]:
    return sorted(codepoint for codepoint in cmap if _keep_codepoint(codepoint))


def _format_unicodes(codepoints: list[int]) -> str:
    ranges: list[str] = []
    first = previous = codepoints[0]
    for codepoint in codepoints[1:]:
        if codepoint == previous + 1:
            previous = codepoint
            continue
        ranges.append(
            f"U+{first:04X}" if first == previous else f"U+{first:04X}-{previous:04X}"
        )
        first = previous = codepoint
    ranges.append(
        f"U+{first:04X}" if first == previous else f"U+{first:04X}-{previous:04X}"
    )
    return ",".join(ranges)


def _name_records(font: TTFont) -> set[tuple[int, int, int, int, bytes]]:
    return {
        (row.nameID, row.platformID, row.platEncID, row.langID, bytes(row.string))
        for row in font["name"].names
    }


def _verify(source_data: bytes, subset_data: bytes) -> tuple[int, int, int]:
    source = TTFont(io.BytesIO(source_data))
    built = TTFont(io.BytesIO(subset_data))
    source_cmap = source.getBestCmap()
    built_cmap = built.getBestCmap()

    source_missing = [codepoint for codepoint in REQUIRED if codepoint not in source_cmap]
    if source_missing:
        points = ", ".join(f"U+{point:04X}" for point in source_missing)
        raise ValueError(f"source font is missing {points}")

    selected = _selected_codepoints(source_cmap)
    missing = [codepoint for codepoint in selected if codepoint not in built_cmap]
    if missing:
        points = ", ".join(f"U+{point:04X}" for point in missing)
        raise ValueError(f"launcher font is missing {points}")

    for codepoint, built_name in built_cmap.items():
        source_name = source_cmap.get(codepoint)
        if source_name is None:
            continue
        if source["hmtx"][source_name] != built["hmtx"][built_name]:
            raise ValueError(f"advance changed for U+{codepoint:04X}")

    if _name_records(source) != _name_records(built):
        raise ValueError("font identity or licence records changed")
    if len(subset_data) >= len(source_data):
        raise ValueError("subset did not reduce the uncompressed font")
    return len(source_cmap), len(selected), len(built_cmap)


def build(source_path: Path, output_zip: Path) -> None:
    source_data = _font_data(source_path)
    source_font = TTFont(io.BytesIO(source_data))
    selected = _selected_codepoints(source_font.getBestCmap())
    source_font.close()
    if not selected:
        raise ValueError("source font has no selected launcher glyphs")
    unicodes = _format_unicodes(selected)
    with tempfile.TemporaryDirectory(prefix="susamune-font-") as temp_dir:
        temp = Path(temp_dir)
        source_ttf = temp / "source.ttf"
        output_ttf = temp / "font.ttf"
        source_ttf.write_bytes(source_data)
        result = subset.main(
            [
                str(source_ttf),
                f"--output-file={output_ttf}",
                f"--unicodes={unicodes}",
                "--recommended-glyphs",
                "--notdef-glyph",
                "--notdef-outline",
                "--name-IDs=*",
                "--name-languages=*",
                "--name-legacy",
            ]
        )
        if result:
            raise RuntimeError(f"fonttools subset failed with status {result}")
        subset_data = output_ttf.read_bytes()

    source_chars, selected_chars, subset_chars = _verify(source_data, subset_data)
    archive_buffer = io.BytesIO()
    info = zipfile.ZipInfo("font.ttf", date_time=(1980, 1, 1, 0, 0, 0))
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    info.compress_type = zipfile.ZIP_DEFLATED
    with zipfile.ZipFile(
        archive_buffer, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as archive:
        archive.writestr(info, subset_data)

    output_zip.parent.mkdir(parents=True, exist_ok=True)
    output_zip.write_bytes(archive_buffer.getvalue())
    print(
        f"font: {len(source_data)} -> {len(subset_data)} bytes; "
        f"cmap: {source_chars} -> {selected_chars} selected -> {subset_chars}; "
        f"container: {source_path.stat().st_size} -> {output_zip.stat().st_size} bytes"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output_zip", type=Path)
    args = parser.parse_args()
    build(args.source, args.output_zip)


if __name__ == "__main__":
    main()
