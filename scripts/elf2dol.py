"""Drop-in replacement for devkitPro's `elf2dol`: convert an ELF into a
GameCube/Wii DOL. The bundled Nintendont toolchain ships without it, and the
wii_rules `%.dol: %.elf` rule relies on it.

Usage: elf2dol IN.elf OUT.dol
"""
import struct
import sys

from elftools.elf.elffile import ELFFile

SHF_WRITE = 0x1
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4
HEADER_SIZE = 0x100
DOL_SECTION_ALIGN = 32

# C-only Wii binaries do not use DWARF exception unwinding. GCC still emits
# unwind tables for prebuilt libraries, so omit those two metadata sections
# while retaining every other allocated section. Refuse the optimization if a
# future link gains a conventional consumer instead of producing a broken DOL.
OMITTED_UNWIND_SECTIONS = frozenset((".eh_frame", ".eh_frame_hdr"))
UNWIND_CONSUMER_PREFIXES = (
    "_Unwind_",
    "__cxa_begin_catch",
    "__cxa_call_unexpected",
    "__cxa_end_catch",
    "__cxa_rethrow",
    "__cxa_throw",
    "__deregister_frame",
    "__gcc_personality",
    "__gxx_personality",
    "__register_frame",
    "backtrace",
)


def _find_unwind_consumer(elf):
    saw_symbol_table = False
    for section in elf.iter_sections():
        if not hasattr(section, "iter_symbols"):
            continue
        saw_symbol_table = True
        for symbol in section.iter_symbols():
            if symbol.name.startswith(UNWIND_CONSUMER_PREFIXES):
                return saw_symbol_table, symbol.name
    return saw_symbol_table, None


def _append_section(sections, addr, payload):
    """Append one initialized range, merging only exactly adjacent ranges."""
    if sections and sections[-1][0] + len(sections[-1][1]) == addr:
        prev_addr, prev_payload = sections[-1]
        sections[-1] = (prev_addr, prev_payload + payload)
    else:
        sections.append((addr, payload))


def _align_load_ranges(sections):
    """Align DOL load addresses, padding only the unmapped prefix with zero."""
    aligned = []
    for addr, payload in sections:
        prefix = addr % DOL_SECTION_ALIGN
        load_addr = addr - prefix
        load_payload = b"\x00" * prefix + payload
        if aligned and load_addr < aligned[-1][0] + len(aligned[-1][1]):
            raise RuntimeError("aligned DOL load ranges overlap")
        _append_section(aligned, load_addr, load_payload)
    return aligned


def _allocated_sections(elf):
    text = []
    data = []
    bss = []
    omitted_unwind = False

    allocated = []
    for section in elf.iter_sections():
        flags = section["sh_flags"]
        size = section["sh_size"]
        if not flags & SHF_ALLOC or size == 0:
            continue
        allocated.append(section)

    allocated.sort(key=lambda section: section["sh_addr"])
    initialized_end = 0
    for section in allocated:
        name = section.name
        section_type = section["sh_type"]
        addr = section["sh_addr"]
        size = section["sh_size"]
        end = addr + size
        if addr < 0 or end > 0x100000000:
            raise RuntimeError("allocated ELF section is outside 32-bit memory")

        if name in OMITTED_UNWIND_SECTIONS:
            if (section_type != "SHT_PROGBITS" or
                    section["sh_flags"] & SHF_EXECINSTR):
                raise RuntimeError("unexpected attributes on " + name)
            omitted_unwind = True
            continue

        if section_type == "SHT_NOBITS":
            bss.append((addr, size))
            continue
        if section_type != "SHT_PROGBITS":
            raise RuntimeError(
                "unsupported allocated ELF section {} ({})".format(
                    name, section_type))
        payload = section.data()
        if len(payload) != size:
            raise RuntimeError("short ELF section data for " + name)
        if addr < initialized_end:
            raise RuntimeError("overlapping initialized ELF section " + name)
        initialized_end = end
        target = text if section["sh_flags"] & SHF_EXECINSTR else data
        _append_section(target, addr, payload)

    # ELF subsections may begin inside an aligned output range (for example,
    # .got2 directly after a non-32-byte-sized .data). A surviving range can
    # also follow omitted unwind bytes at an unaligned address. Align the DOL
    # load itself down and zero only that unused prefix; all ELF bytes retain
    # their original addresses.
    text = _align_load_ranges(text)
    data = _align_load_ranges(data)
    load_ranges = sorted(text + data)
    for previous, current in zip(load_ranges, load_ranges[1:]):
        if previous[0] + len(previous[1]) > current[0]:
            raise RuntimeError("aligned DOL text/data ranges overlap")

    if omitted_unwind:
        saw_symbol_table, consumer = _find_unwind_consumer(elf)
        if not saw_symbol_table:
            raise RuntimeError(
                "cannot audit unwind consumers without an ELF symbol table")
        if consumer is not None:
            raise RuntimeError(
                "cannot omit unwind metadata: linked consumer " + consumer)

    return text, data, bss


def build_dol(elf):
    text, data, bss = _allocated_sections(elf)

    text_off = [0] * 7
    text_addr = [0] * 7
    text_size = [0] * 7
    data_off = [0] * 11
    data_addr = [0] * 11
    data_size = [0] * 11
    blob = bytearray()

    def place(sections, offs, addrs, sizes, limit):
        for i, (addr, payload) in enumerate(sections):
            if i >= limit:
                raise RuntimeError("too many DOL sections")
            offs[i] = HEADER_SIZE + len(blob)
            addrs[i] = addr
            sizes[i] = len(payload)
            blob.extend(payload)
            blob.extend(b"\x00" * (-len(blob) % DOL_SECTION_ALIGN))

    place(text, text_off, text_addr, text_size, 7)
    place(data, data_off, data_addr, data_size, 11)

    bss_addr = min((addr for addr, _size in bss), default=0)
    bss_end = max((addr + size for addr, size in bss), default=0)
    bss_size = bss_end - bss_addr
    if bss_addr % DOL_SECTION_ALIGN:
        raise RuntimeError("DOL BSS is not 32-byte aligned")
    for addr, payload in text + data:
        if bss_addr < addr + len(payload) and addr < bss_end:
            raise RuntimeError("DOL BSS overlaps an initialized section")

    header = struct.pack(
        ">18I18I18I2II",
        *text_off, *data_off,
        *text_addr, *data_addr,
        *text_size, *data_size,
        bss_addr, bss_size,
        elf["e_entry"],
    )
    header = header.ljust(HEADER_SIZE, b"\x00")
    return header + bytes(blob)


def main(argv):
    with open(argv[0], "rb") as f:
        dol = build_dol(ELFFile(f))
    with open(argv[1], "wb") as f:
        f.write(dol)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
