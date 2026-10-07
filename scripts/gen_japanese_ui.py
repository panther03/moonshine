"""Build the heapless JP catalogue and a bounded subset of Dolphin's Droid font."""
from pathlib import Path
import argparse
import json
import re
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
MAX_SIZE = 0x19000
MAGIC = 0x4D4A5549
VERSION = 1
TOKENS = dict(zip('A B X Y L R Z C AMP SLASH LEFT UP RIGHT DOWN SHINE'.split(),
                 '8197 8194 817b 818f 8183 8184 8190 8193 8195 815e 81a9 81aa 81a8 81ab 819a'.split()))
JA_TOKENS = dict(TOKENS, **dict(zip('A B X Y L R Z C'.split(),
                                  '8260 8261 8277 8278 826b 8271 8279 8262'.split())))
SPEC = re.compile(r'%(?:[-+ #0]*)(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|h|ll|l|j|z|t|L)?[diuoxXfFeEgGaAcspn%]')


def expand(text, tokens):
    parts = re.split(r'(\{[^}]*\})', text)
    return b''.join(bytes.fromhex(tokens[p[1:-1]]) if p.startswith('{') else p.encode('cp932') for p in parts)


def hashes(data):
    h, f = 0x811C9DC5, 5381
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
        f = ((f * 33) ^ b) & 0xFFFF
    return h, f


def catalogue(path):
    result, seen = [], set()
    for number, line in enumerate(path.read_text(encoding='utf-8-sig').splitlines(), 1):
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        if line.count('\t') != 1:
            raise ValueError(f'line {number}: expected one tab')
        en, ja = line.split('\t')
        if ([s for s in SPEC.findall(en) if s != '%%'] !=
                [s for s in SPEC.findall(ja) if s != '%%']) or '%n' in en:
            raise ValueError(f'line {number}: incompatible format arguments')
        key, value = expand(en, TOKENS), expand(ja, JA_TOKENS)
        if not key or b'\0' in key + value or hashes(key)[0] in seen:
            raise ValueError(f'line {number}: empty/duplicate/colliding key')
        seen.add(hashes(key)[0])
        result.append((hashes(key), value))
    return sorted(result)


def yay0(data):
    if data[:4] != b'Yay0':
        raise ValueError('expected Yay0 font')
    size, link, chunk = struct.unpack_from('>III', data, 4)
    if size > 0x200000:
        raise ValueError('font is too large')
    out, mask, bits, cursor = bytearray(), 0, 0, 16
    while len(out) < size:
        if not bits:
            mask, bits = data[cursor], 8
            cursor += 1
        if mask & 0x80:
            out.append(data[chunk]); chunk += 1
        else:
            word = struct.unpack_from('>H', data, link)[0]; link += 2
            length, distance = word >> 12, (word & 4095) + 1
            if not length:
                length = data[chunk] + 18; chunk += 1
            else:
                length += 2
            if distance > len(out) or length > size - len(out):
                raise ValueError('invalid font back-reference')
            for _ in range(length):
                out.append(out[-distance])
        bits -= 1; mask <<= 1
    return out


def font_glyph(font, index):
    image = struct.unpack_from('>I', font, 0x24)[0]
    page, local = divmod(index, 21 * 21)
    base_x, base_y = local % 21 * 24, local // 21 * 24 + page * 512
    def pixel(x, y):
        x += base_x; y += base_y
        offset = image + (y // 8 * 64 + x // 8) * 16 + y % 8 * 2 + x % 8 // 4
        return (font[offset] >> (6 - 2 * (x % 4))) & 3
    # Exact area filter from 24 to 16 pixels, retaining the source's four shades.
    pixels = []
    for y in range(16):
        for x in range(16):
            total = 0
            for iy in range(3*y//2, (3*y+2)//2+1):
                wy = max(0, min(3*y+3, 2*iy+2) - max(3*y, 2*iy))
                for ix in range(3*x//2, (3*x+2)//2+1):
                    wx = max(0, min(3*x+3, 2*ix+2) - max(3*x, 2*ix))
                    if wx and wy:
                        total += pixel(ix, iy) * wx * wy
            pixels.append((total + 4) // 9)
    packed = bytearray()
    for by in (0, 8):
        for bx in (0, 8):
            for y in range(8):
                for x in (0, 4):
                    packed.append(sum(pixels[(by+y)*16+bx+x+i] << (6-2*i) for i in range(4)))
    return font[0x30 + index], packed


def supplemental_glyphs(path=ROOT/'data/fonts/noto-japanese-supplement.json'):
    result = {}
    for key, glyph in json.loads(path.read_text(encoding='utf-8'))['glyphs'].items():
        code, width, image = int(key, 16), glyph['width'], bytes.fromhex(glyph['image'])
        chr(code).encode('cp932')
        if code in result or not 1 <= width <= 24 or len(image) != 64 or not any(image):
            raise ValueError(f'invalid supplemental glyph U+{code:04X}')
        result[code] = width, image
    return result


def build(path=ROOT/'data/japanese_ui.tsv'):
    rows = catalogue(path)
    pool, offsets = bytearray(), {}
    for value in sorted({value for _, value in rows}):
        offsets[value] = len(pool); pool.extend(value + b'\0')
    if len(pool) > 65535:
        raise ValueError('text pool exceeds 16-bit offsets')
    table = b''.join(struct.pack('>IHH', *key, offsets[value]) for key, value in rows)
    codes = {ord(c) for _, value in rows for c in value.decode('cp932')}
    codes.update(range(32, 127))
    codes.update(ord(bytes.fromhex(v).decode('cp932')) for v in JA_TOKENS.values())
    source_codes = json.loads((ROOT/'data/fonts/droid-japanese-codepoints.json').read_text())
    source_map = {code: i for i, code in reversed(list(enumerate(source_codes)))}
    source_map[0x7E] = source_map[0x203E]
    font = yay0((ROOT/'data/fonts/droid-japanese.yay').read_bytes())
    supplement = supplemental_glyphs()
    if source_map.keys() & supplement.keys():
        raise ValueError('supplemental glyph would replace a Droid glyph')
    glyphs = {}
    for code in codes:
        if code in source_map:
            glyph = font_glyph(font, source_map[code])
        elif code in supplement:
            glyph = supplement[code]
        else:
            raise ValueError(f'font lacks U+{code:04X}')
        encoded = chr(code).encode('cp932')
        glyphs[int.from_bytes(encoded, 'big')] = glyph
    for name, old in TOKENS.items():
        glyphs[int(old, 16)] = glyphs[int(JA_TOKENS[name], 16)]
    glyph_table, pixels = bytearray(), bytearray()
    for code, (width, image) in sorted(glyphs.items()):
        glyph_table.extend(struct.pack('>HBB', code, width, 0)); pixels.extend(image)
    text_offset = 64 + len(table)
    glyph_offset = text_offset + len(pool)
    pixel_offset = (glyph_offset + len(glyph_table) + 31) & ~31
    payload = table + pool + glyph_table + bytes(pixel_offset-glyph_offset-len(glyph_table)) + pixels
    header = struct.pack('>16I', MAGIC, VERSION, 64+len(payload), 0, 64, len(rows),
                         text_offset, len(pool), glyph_offset, len(glyphs), pixel_offset, 16, 64, 0, 0, 0)
    result = bytearray(header + payload)
    struct.pack_into('>I', result, 12, zlib.crc32(result))
    if len(result) > MAX_SIZE:
        raise ValueError(f'JP asset {len(result)} exceeds {MAX_SIZE}')
    return result, dict(bytes=len(result), entries=len(rows), text_bytes=len(pool), glyphs=len(glyphs), glyph_bytes=len(pixels))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    data, report = build()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    args.output.with_suffix('.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
