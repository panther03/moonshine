#!/usr/bin/env python3
"""List direct PPC calls to a symbol in a retail DOL."""

from __future__ import annotations

import argparse
import re
import struct
from collections import Counter
from dataclasses import dataclass
from pathlib import Path


MAP_SYMBOL_RE = re.compile(
    r"^\s+[0-9a-fA-F]{8}\s+([0-9a-fA-F]{6})\s+"
    r"([0-9a-fA-F]{8})\s+\d+\s+(\S+)\s+(.+)$"
)


@dataclass(frozen=True)
class Symbol:
    address: int
    size: int
    name: str
    object_file: str


def read_symbols(path: Path) -> list[Symbol]:
    symbols = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = MAP_SYMBOL_RE.match(line)
        if not match:
            continue
        size = int(match.group(1), 16)
        address = int(match.group(2), 16)
        if size and 0x80000000 <= address < 0x81800000:
            symbols.append(
                Symbol(address, size, match.group(3), match.group(4).strip())
            )
    return symbols


def dol_text_sections(data: bytes):
    for index in range(7):
        file_offset = struct.unpack_from(">I", data, index * 4)[0]
        address = struct.unpack_from(">I", data, 0x48 + index * 4)[0]
        size = struct.unpack_from(">I", data, 0x90 + index * 4)[0]
        if size:
            yield address, data[file_offset : file_offset + size]


def sign_extend_26(value: int) -> int:
    return value - 0x04000000 if value & 0x02000000 else value


def direct_calls(data: bytes, target: int, include_tail_branches: bool):
    for section_address, section in dol_text_sections(data):
        for offset in range(0, len(section), 4):
            word = struct.unpack_from(">I", section, offset)[0]
            if word >> 26 != 18 or (not include_tail_branches and not word & 1):
                continue
            displacement = sign_extend_26(word & 0x03FFFFFC)
            destination = (
                displacement
                if word & 2
                else section_address + offset + displacement
            )
            if destination & 0xFFFFFFFF == target:
                yield section_address + offset


def containing_symbol(symbols: list[Symbol], address: int) -> Symbol | None:
    candidates = [
        symbol
        for symbol in symbols
        if symbol.address <= address < symbol.address + symbol.size
        and not symbol.name.startswith(".")
    ]
    return min(candidates, key=lambda symbol: symbol.size, default=None)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dol", type=Path)
    parser.add_argument("map", type=Path)
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument("--target", type=lambda value: int(value, 0))
    target.add_argument("--target-symbol")
    parser.add_argument(
        "--group", action="store_true", help="group results by containing function"
    )
    parser.add_argument(
        "--include-tail-branches",
        action="store_true",
        help="include direct b instructions as well as bl calls",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    symbols = read_symbols(args.map)
    target = args.target
    if target is None:
        matches = [symbol for symbol in symbols if symbol.name == args.target_symbol]
        if not matches:
            raise SystemExit(f"symbol not found: {args.target_symbol}")
        target = min(matches, key=lambda symbol: symbol.size).address

    rows = []
    for address in direct_calls(
        args.dol.read_bytes(), target, args.include_tail_branches
    ):
        symbol = containing_symbol(symbols, address)
        if symbol is None:
            rows.append((address, 0, "<unknown>", "<unknown>"))
        else:
            rows.append(
                (address, address - symbol.address, symbol.name, symbol.object_file)
            )

    if args.group:
        counts = Counter((name, object_file) for _, _, name, object_file in rows)
        for (name, object_file), count in sorted(counts.items()):
            print(f"{count:3}\t{name}\t{object_file}")
    else:
        for address, offset, name, object_file in rows:
            print(f"{address:08X}\t+0x{offset:X}\t{name}\t{object_file}")
    print(f"total\t{len(rows)}")


if __name__ == "__main__":
    main()
