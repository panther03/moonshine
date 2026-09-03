"""Verify that a DOL preserves every retained allocated ELF byte."""

from __future__ import annotations

import argparse
from pathlib import Path
import struct

from elftools.elf.elffile import ELFFile


OMITTED = frozenset((".eh_frame", ".eh_frame_hdr"))
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4


def _dol_ranges(data: bytes):
    if len(data) < 0x100:
        raise ValueError("short DOL header")
    fields = struct.unpack_from(">57I", data)
    offsets = fields[0:18]
    addresses = fields[18:36]
    sizes = fields[36:54]
    ranges = []
    for index, (offset, address, size) in enumerate(
            zip(offsets, addresses, sizes)):
        if size == 0:
            continue
        if offset < 0x100 or offset + size > len(data):
            raise ValueError("DOL section is outside the file")
        if offset % 32 or address % 32:
            raise ValueError("DOL section is not 32-byte aligned")
        kind = "text" if index < 7 else "data"
        ranges.append((kind, address, data[offset:offset + size]))
    return ranges, fields[54], fields[55], fields[56]


def verify(elf_path: Path, dol_path: Path) -> tuple[int, int, int]:
    dol_data = dol_path.read_bytes()
    ranges, bss_addr, bss_size, entry = _dol_ranges(dol_data)

    with elf_path.open("rb") as stream:
        elf = ELFFile(stream)
        retained = []
        bss = []
        for section in elf.iter_sections():
            flags = section["sh_flags"]
            size = section["sh_size"]
            if not flags & SHF_ALLOC or size == 0:
                continue
            if section.name in OMITTED:
                continue
            if section["sh_type"] == "SHT_NOBITS":
                bss.append((section["sh_addr"], size))
                continue
            if section["sh_type"] != "SHT_PROGBITS":
                raise ValueError("unsupported retained section " + section.name)
            kind = "text" if flags & SHF_EXECINSTR else "data"
            retained.append((kind, section.name, section["sh_addr"],
                             section.data()))

        if entry != elf["e_entry"]:
            raise ValueError("DOL entry point differs from ELF")

    expected_bss_addr = min((addr for addr, _size in bss), default=0)
    expected_bss_end = max((addr + size for addr, size in bss), default=0)
    if (bss_addr, bss_size) != (
            expected_bss_addr, expected_bss_end - expected_bss_addr):
        raise ValueError("DOL BSS span differs from ELF")

    covered = set()
    for kind, load_addr, payload in ranges:
        expected = bytearray(len(payload))
        used = bytearray(len(payload))
        first_retained = None
        for index, (section_kind, name, addr, section_data) in enumerate(retained):
            if section_kind != kind:
                continue
            start = addr - load_addr
            end = start + len(section_data)
            if start < 0 or end > len(payload):
                continue
            if any(used[start:end]):
                raise ValueError("retained ELF sections overlap in DOL")
            expected[start:end] = section_data
            used[start:end] = b"\x01" * len(section_data)
            covered.add(index)
            first_retained = start if first_retained is None else min(
                first_retained, start)
        if first_retained is None:
            raise ValueError("DOL range contains no retained ELF section")
        if first_retained >= 32:
            raise ValueError("DOL range has more than alignment-prefix padding")
        if payload != expected:
            raise ValueError("DOL payload differs from retained ELF bytes")

    if covered != set(range(len(retained))):
        missing = [retained[index][1]
                   for index in set(range(len(retained))) - covered]
        raise ValueError("DOL is missing retained sections: " + ", ".join(missing))

    retained_bytes = sum(len(section_data)
                         for _kind, _name, _addr, section_data in retained)
    padding_bytes = sum(len(payload) for _kind, _addr, payload in ranges)
    padding_bytes -= retained_bytes
    return len(retained), retained_bytes, padding_bytes


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=Path)
    parser.add_argument("dol", type=Path)
    args = parser.parse_args()
    count, retained_bytes, padding_bytes = verify(args.elf, args.dol)
    print("verified {} retained sections, {} bytes, {} zero padding bytes".format(
        count, retained_bytes, padding_bytes))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
