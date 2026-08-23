import importlib.util
from pathlib import Path
import struct
import unittest


ELF2DOL_PATH = Path(__file__).with_name("elf2dol.py")
spec = importlib.util.spec_from_file_location("elf2dol_test", ELF2DOL_PATH)
assert spec and spec.loader
elf2dol = importlib.util.module_from_spec(spec)
spec.loader.exec_module(elf2dol)


class FakeSection:
    def __init__(self, name, addr=0, payload=b"", section_type="SHT_PROGBITS",
                 flags=0, size=None):
        self.name = name
        self._payload = payload
        self._fields = {
            "sh_addr": addr,
            "sh_flags": flags,
            "sh_size": len(payload) if size is None else size,
            "sh_type": section_type,
        }

    def __getitem__(self, key):
        return self._fields[key]

    def data(self):
        return self._payload


class FakeSymbol:
    def __init__(self, name):
        self.name = name


class FakeSymbolTable(FakeSection):
    def __init__(self, names):
        super().__init__(".symtab")
        self._symbols = [FakeSymbol(name) for name in names]

    def iter_symbols(self):
        return iter(self._symbols)


class FakeElf:
    def __init__(self, sections, entry=0x80004000):
        self._sections = sections
        self._entry = entry

    def __getitem__(self, key):
        if key != "e_entry":
            raise KeyError(key)
        return self._entry

    def iter_sections(self):
        return iter(self._sections)


def parse_dol(dol):
    fields = struct.unpack_from(">57I", dol)
    text_off = fields[0:7]
    data_off = fields[7:18]
    text_addr = fields[18:25]
    data_addr = fields[25:36]
    text_size = fields[36:43]
    data_size = fields[43:54]

    def ranges(offsets, addrs, sizes):
        return [
            (addr, dol[offset:offset + size], offset)
            for offset, addr, size in zip(offsets, addrs, sizes)
            if size
        ]

    return {
        "text": ranges(text_off, text_addr, text_size),
        "data": ranges(data_off, data_addr, data_size),
        "bss_addr": fields[54],
        "bss_size": fields[55],
        "entry": fields[56],
    }


class Elf2DolTests(unittest.TestCase):
    def test_preserves_allocated_sections_and_omits_unwind_metadata(self):
        alloc = elf2dol.SHF_ALLOC
        executable = alloc | elf2dol.SHF_EXECINSTR
        sections = [
            FakeSection(".data", 0x80005200, b"D" * 0x21, flags=alloc),
            FakeSection(".eh_frame", 0x80005120, b"E" * 0x80,
                        flags=alloc | elf2dol.SHF_WRITE),
            FakeSection(".text", 0x80004020, b"T" * 0x40, flags=executable),
            FakeSection(".debug_info", payload=b"debug"),
            FakeSection(".init", 0x80004000, b"I" * 0x20, flags=executable),
            FakeSection(".rodata", 0x80005000, b"R" * 0x24, flags=alloc),
            FakeSection(".eh_frame_hdr", 0x80005024, b"H" * 0x7c,
                        flags=alloc),
            FakeSection(".sbss", 0x80006000, section_type="SHT_NOBITS",
                        flags=alloc | elf2dol.SHF_WRITE, size=0x20),
            FakeSection(".bss", 0x80006020, section_type="SHT_NOBITS",
                        flags=alloc | elf2dol.SHF_WRITE, size=0x40),
            FakeSymbolTable([]),
        ]

        parsed = parse_dol(elf2dol.build_dol(FakeElf(sections)))

        self.assertEqual(parsed["text"], [
            (0x80004000, b"I" * 0x20 + b"T" * 0x40, 0x100),
        ])
        self.assertEqual(
            [(addr, payload) for addr, payload, _offset in parsed["data"]],
            [(0x80005000, b"R" * 0x24), (0x80005200, b"D" * 0x21)])
        self.assertTrue(all(offset % 32 == 0
                            for _addr, _payload, offset in parsed["data"]))
        self.assertEqual(parsed["bss_addr"], 0x80006000)
        self.assertEqual(parsed["bss_size"], 0x60)
        self.assertEqual(parsed["entry"], 0x80004000)

    def test_only_exact_unwind_section_names_are_omitted(self):
        alloc = elf2dol.SHF_ALLOC
        sections = [
            FakeSection(".debug_frame", 0x80005000, b"keep", flags=alloc),
            FakeSection(".eh_frame", 0x80005100, b"drop", flags=alloc),
            FakeSymbolTable([]),
        ]
        parsed = parse_dol(elf2dol.build_dol(FakeElf(sections)))
        self.assertEqual(
            [(addr, payload) for addr, payload, _offset in parsed["data"]],
            [(0x80005000, b"keep")])

    def test_rejects_linked_unwind_consumer(self):
        sections = [
            FakeSection(".eh_frame", 0x80005000, b"frames",
                        flags=elf2dol.SHF_ALLOC),
            FakeSymbolTable(["ordinary_function", "_Unwind_Resume"]),
        ]
        with self.assertRaisesRegex(RuntimeError, "_Unwind_Resume"):
            elf2dol.build_dol(FakeElf(sections))

    def test_rejects_unwind_omission_without_symbol_table(self):
        sections = [
            FakeSection(".eh_frame", 0x80005000, b"frames",
                        flags=elf2dol.SHF_ALLOC),
        ]
        with self.assertRaisesRegex(RuntimeError, "without an ELF symbol table"):
            elf2dol.build_dol(FakeElf(sections))

    def test_rejects_executable_unwind_section(self):
        sections = [
            FakeSection(".eh_frame", 0x80005000, b"frames", flags=(
                elf2dol.SHF_ALLOC | elf2dol.SHF_EXECINSTR)),
        ]
        with self.assertRaisesRegex(RuntimeError, "unexpected attributes"):
            elf2dol.build_dol(FakeElf(sections))

    def test_rejects_unrepresentable_allocated_section_type(self):
        sections = [
            FakeSection(".init_array", 0x80005000, b"array",
                        section_type="SHT_INIT_ARRAY",
                        flags=elf2dol.SHF_ALLOC | elf2dol.SHF_WRITE),
        ]
        with self.assertRaisesRegex(RuntimeError, "SHT_INIT_ARRAY"):
            elf2dol.build_dol(FakeElf(sections))

    def test_aligns_misaligned_load_range_without_moving_elf_bytes(self):
        sections = [
            FakeSection(".data", 0x80005004, b"data",
                        flags=elf2dol.SHF_ALLOC | elf2dol.SHF_WRITE),
        ]
        parsed = parse_dol(elf2dol.build_dol(FakeElf(sections)))
        self.assertEqual(
            [(addr, payload) for addr, payload, _offset in parsed["data"]],
            [(0x80005000, b"\0" * 4 + b"data")])

    def test_allows_misaligned_subsection_inside_aligned_load_range(self):
        alloc = elf2dol.SHF_ALLOC
        sections = [
            FakeSection(".data", 0x80005000, b"D" * 0x24,
                        flags=alloc | elf2dol.SHF_WRITE),
            FakeSection(".got2", 0x80005024, b"G" * 0x1c,
                        flags=alloc | elf2dol.SHF_WRITE),
        ]
        parsed = parse_dol(elf2dol.build_dol(FakeElf(sections)))
        self.assertEqual(
            [(addr, payload) for addr, payload, _offset in parsed["data"]],
            [(0x80005000, b"D" * 0x24 + b"G" * 0x1c)])

    def test_rejects_alignment_padding_overlapping_another_load_range(self):
        alloc = elf2dol.SHF_ALLOC
        sections = [
            FakeSection(".text", 0x80005000, b"T" * 0x18,
                        flags=alloc | elf2dol.SHF_EXECINSTR),
            FakeSection(".data", 0x8000501c, b"data", flags=alloc),
        ]
        with self.assertRaisesRegex(RuntimeError, "text/data ranges overlap"):
            elf2dol.build_dol(FakeElf(sections))

    def test_rejects_initialized_data_overlapping_bss(self):
        alloc = elf2dol.SHF_ALLOC
        sections = [
            FakeSection(".data", 0x80005000, b"D" * 0x40, flags=alloc),
            FakeSection(".bss", 0x80005020, section_type="SHT_NOBITS",
                        flags=alloc | elf2dol.SHF_WRITE, size=0x40),
        ]
        with self.assertRaisesRegex(RuntimeError, "BSS overlaps"):
            elf2dol.build_dol(FakeElf(sections))

    def test_rejects_overlapping_initialized_sections(self):
        sections = [
            FakeSection(".rodata", 0x80005000, b"R" * 0x40,
                        flags=elf2dol.SHF_ALLOC),
            FakeSection(".data", 0x80005020, b"D" * 0x40,
                        flags=elf2dol.SHF_ALLOC | elf2dol.SHF_WRITE),
        ]
        with self.assertRaisesRegex(RuntimeError, "overlapping initialized"):
            elf2dol.build_dol(FakeElf(sections))

    def test_rejects_misaligned_bss(self):
        sections = [
            FakeSection(".bss", 0x80006008, section_type="SHT_NOBITS",
                        flags=elf2dol.SHF_ALLOC | elf2dol.SHF_WRITE, size=0x40),
        ]
        with self.assertRaisesRegex(RuntimeError, "BSS is not 32-byte aligned"):
            elf2dol.build_dol(FakeElf(sections))

    def test_enforces_dol_section_limit_after_safe_adjacent_merges(self):
        sections = [
            FakeSection(".text{}".format(i), 0x80004000 + i * 0x40,
                        bytes((i,)) * 0x20,
                        flags=(elf2dol.SHF_ALLOC |
                               elf2dol.SHF_EXECINSTR))
            for i in range(8)
        ]
        with self.assertRaisesRegex(RuntimeError, "too many DOL sections"):
            elf2dol.build_dol(FakeElf(sections))


if __name__ == "__main__":
    unittest.main()
