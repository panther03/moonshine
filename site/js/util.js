// Small shared helpers: colour conversion, CRC-32, and a big-endian byte writer.

/** @param {number} rgb 0xRRGGBB */
export const rgbToHex = (rgb) => '#' + (rgb >>> 0).toString(16).padStart(6, '0');
/** @param {string} s '#rrggbb' */
export const hexToRgb = (s) => parseInt(s.slice(1), 16);

/** CSS colour from the split rgb/alpha pair the config stores. */
export const rgba = (rgb, a) =>
  `rgba(${(rgb >> 16) & 0xff}, ${(rgb >> 8) & 0xff}, ${rgb & 0xff}, ${(a & 0xff) / 255})`;

/** Pack to the RGBA8888 word the blob stores. */
export const packRGBA = (rgb, a) => (((rgb & 0xffffff) << 8) | (a & 0xff)) >>> 0;

export const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);

const crcTable = (() => {
  const t = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[i] = c >>> 0;
  }
  return t;
})();

/** CRC-32 (IEEE), the variant the mod-side reader will use. */
export function crc32(bytes) {
  let c = 0xffffffff;
  for (let i = 0; i < bytes.length; i++) c = crcTable[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

/** Growable big-endian byte sink. Both sides of the handoff are big-endian. */
export class ByteWriter {
  constructor() {
    this.bytes = [];
  }
  get length() {
    return this.bytes.length;
  }
  u8(v) {
    this.bytes.push(v & 0xff);
    return this;
  }
  s8(v) {
    return this.u8(v < 0 ? v + 0x100 : v);
  }
  u16(v) {
    return this.u8(v >> 8).u8(v);
  }
  s16(v) {
    return this.u16(v < 0 ? v + 0x10000 : v);
  }
  u32(v) {
    return this.u8(v >>> 24).u8(v >>> 16).u8(v >>> 8).u8(v);
  }
  raw(arr) {
    for (const b of arr) this.u8(b);
    return this;
  }
  /** Pad with zeroes until the length is a multiple of `n`. */
  align(n) {
    while (this.length % n) this.u8(0);
    return this;
  }
  /** Overwrite an already-written big-endian u32 (used to backfill size/crc). */
  patchU32(off, v) {
    this.bytes[off] = (v >>> 24) & 0xff;
    this.bytes[off + 1] = (v >>> 16) & 0xff;
    this.bytes[off + 2] = (v >>> 8) & 0xff;
    this.bytes[off + 3] = v & 0xff;
  }
  toUint8Array() {
    return Uint8Array.from(this.bytes);
  }
}

export function toBase64(bytes) {
  let s = '';
  for (let i = 0; i < bytes.length; i++) s += String.fromCharCode(bytes[i]);
  return btoa(s);
}

export function toHexDump(bytes) {
  const lines = [];
  for (let i = 0; i < bytes.length; i += 16) {
    const row = Array.from(bytes.slice(i, i + 16), (b) =>
      b.toString(16).toUpperCase().padStart(2, '0'),
    );
    const cols = [];
    for (let j = 0; j < row.length; j += 4) cols.push(row.slice(j, j + 4).join(''));
    lines.push(i.toString(16).toUpperCase().padStart(4, '0') + '  ' + cols.join(' '));
  }
  return lines.join('\n');
}
