#!/usr/bin/env python3
"""Copy only JP visual theme values between Moonshine ini files."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


TARGET_SECTIONS = {
    "input_display_jp",
    "metadata_display_jp",
    "qft_display_jp",
    "creation_jp",
}

INPUT_KEYS = {
    "background_r",
    "background_g",
    "background_b",
    "background_alpha",
    "brightness",
    "element_alpha",
    "padding",
    "main_stick_rgb",
    "c_stick_rgb",
    "a_rgb",
    "b_rgb",
    "x_rgb",
    "y_rgb",
    "l_rgb",
    "r_rgb",
    "start_rgb",
    "z_rgb",
    "value_rgb",
    "trigger_outline_rgb",
}

METADATA_KEYS = {
    "text_r",
    "text_g",
    "text_b",
    "text_alpha",
    "background_r",
    "background_g",
    "background_b",
    "background_alpha",
    "text_brightness",
    "padding",
}

QFT_KEYS = {
    *(f"text_{index}_rgb" for index in range(1, 10)),
    "text_alpha",
    "background_r",
    "background_g",
    "background_b",
    "background_alpha",
    "text_brightness",
    "padding",
}

SECTION_RE = re.compile(r"^\s*\[([^]]+)]\s*$")
VALUE_RE = re.compile(r"^([A-Za-z0-9_]+)(\s*=\s*)(.*?)(\r?\n)?$")


def is_allowed(section: str, key: str) -> bool:
    if section == "input_display_jp":
        return key in INPUT_KEYS
    if section == "metadata_display_jp":
        return key in METADATA_KEYS
    if section == "qft_display_jp":
        return key in QFT_KEYS
    if section == "creation_jp":
        return key.endswith(("_rgb", "_alpha", "_brightness", "_padding"))
    return False


def read_visual_values(path: Path) -> dict[tuple[str, str], str]:
    values: dict[tuple[str, str], str] = {}
    section = ""
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        section_match = SECTION_RE.match(raw_line)
        if section_match:
            section = section_match.group(1)
            continue
        value_match = VALUE_RE.match(raw_line)
        if not value_match or not is_allowed(section, value_match.group(1)):
            continue
        identity = (section, value_match.group(1))
        if identity in values:
            raise ValueError(f"duplicate visual key: [{section}] {identity[1]}")
        values[identity] = value_match.group(3)
    return values


def merge(base: Path, theme: Path, output: Path) -> list[tuple[str, str, str, str]]:
    theme_values = read_visual_values(theme)
    with base.open("r", encoding="utf-8", newline="") as base_file:
        base_text = base_file.read()
    section = ""
    seen: set[tuple[str, str]] = set()
    changes: list[tuple[str, str, str, str]] = []
    output_lines: list[str] = []

    for raw_line in base_text.splitlines(keepends=True):
        section_match = SECTION_RE.match(raw_line.rstrip("\r\n"))
        if section_match:
            section = section_match.group(1)
            output_lines.append(raw_line)
            continue

        value_match = VALUE_RE.match(raw_line)
        if not value_match:
            output_lines.append(raw_line)
            continue

        key = value_match.group(1)
        identity = (section, key)
        if identity in seen:
            raise ValueError(f"duplicate base key: [{section}] {key}")
        seen.add(identity)
        if not is_allowed(section, key) or identity not in theme_values:
            output_lines.append(raw_line)
            continue

        old_value = value_match.group(3)
        new_value = theme_values[identity]
        if old_value != new_value:
            changes.append((section, key, old_value, new_value))
        output_lines.append(
            value_match.group(1)
            + value_match.group(2)
            + new_value
            + (value_match.group(4) or "")
        )

    missing_sections = TARGET_SECTIONS - {
        section for section, _ in seen if section in TARGET_SECTIONS
    }
    if missing_sections:
        raise ValueError("base is missing sections: " + ", ".join(sorted(missing_sections)))

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8", newline="") as output_file:
        output_file.write("".join(output_lines))
    return changes


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", type=Path, help="current ini whose state is preserved")
    parser.add_argument("theme", type=Path, help="ini carrying the desired JP palette")
    parser.add_argument("output", type=Path)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    changes = merge(args.base, args.theme, args.output)
    for section, key, old, new in changes:
        print(f"[{section}] {key}: {old} -> {new}")
    print(f"changed {len(changes)} JP visual values")


if __name__ == "__main__":
    main()
