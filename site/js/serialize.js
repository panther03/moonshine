// Writes susamune_gui.bin. FORMAT.md is the spec; keep the two in step.

import { ByteWriter, crc32, packRGBA } from './util.js';
import { TYPES } from './model.js';
import { parseFormat } from './format.js';
import { encodeText } from './text.js';

const MAGIC = 0x53475549; // 'SGUI'
const VERSION = 1;
const HEADER_SIZE = 16;
const FLAG_ENABLED = 0x1;
const STYLE_FLAG_GRADIENT = 0x1;

/** TextStyle -- 24 bytes, shared by element types 0x01-0x05. */
function writeStyle(w, el) {
  const gradient = el.fgRGB2 != null && el.fgA2 != null;
  w.s16(el.x);
  w.s16(el.y);
  w.u8(el.fontSize);
  w.u8(gradient ? STYLE_FLAG_GRADIENT : 0);
  w.s8(el.bgLeft);
  w.s8(el.bgRight);
  w.s8(el.bgTop);
  w.s8(el.bgBot);
  w.u16(0); // reserved
  w.u32(packRGBA(el.fgRGB, el.fgA));
  // fgBottom mirrors fgTop when the gradient is off, so the mod never has to
  // branch on the flag to pick a colour.
  w.u32(gradient ? packRGBA(el.fgRGB2, el.fgA2) : packRGBA(el.fgRGB, el.fgA));
  w.u32(packRGBA(el.bgRGB, el.bgA));
}

function writeBody(w, el, region) {
  switch (el.kind) {
    case 'display': {
      writeStyle(w, el);
      const { format, fieldIds } = parseFormat(el.fmt, region);
      const fmtBytes = encodeText(format, region);
      fmtBytes.push(0); // NUL, counted by fmtLen
      w.u8(fieldIds.length);
      w.u8(fmtBytes.length);
      w.raw(fieldIds);
      w.raw(fmtBytes);
      break;
    }
    case 'qft':
      writeStyle(w, el);
      w.u16(el.freezeDuration);
      w.u16(0);
      break;
    case 'qfst':
    case 'pattern':
      writeStyle(w, el);
      break;
    case 'attempts':
      writeStyle(w, el);
      w.u16(el.duration);
      w.u16(0);
      break;
    case 'controller':
      w.s16(el.x);
      w.s16(el.y);
      w.u16(el.height);
      w.u8(el.lineWidth);
      w.u8(0); // reserved
      w.u32(packRGBA(el.bgRGB, el.bgA));
      break;
    default:
      throw new Error(`cannot serialise element kind "${el.kind}"`);
  }
}

/** @returns {Uint8Array} */
export function serialize(elements, region) {
  const w = new ByteWriter();
  w.u32(MAGIC);
  w.u16(VERSION);
  w.u16(elements.length);
  w.u32(0); // size, backfilled
  w.u32(0); // crc32, backfilled

  for (const el of elements) {
    const start = w.length;
    w.u8(TYPES[el.kind].typeId);
    w.u8(FLAG_ENABLED);
    w.u16(0); // size, backfilled once the body is padded
    writeBody(w, el, region);
    w.align(4);
    // A reader skips unknown types by this size, so it has to cover the header.
    const size = w.length - start;
    w.bytes[start + 2] = (size >> 8) & 0xff;
    w.bytes[start + 3] = size & 0xff;
  }

  w.patchU32(8, w.length);
  const bytes = w.toUint8Array();
  // The CRC covers everything past the CRC field itself.
  const crc = crc32(bytes.subarray(HEADER_SIZE));
  w.patchU32(12, crc);
  return w.toUint8Array();
}
