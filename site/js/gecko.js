// Emits the Gecko code that overwrites the mod's pinned appearance config.
// FORMAT.md is the spec; keep the two in step.
//
// The mod ships its own defaults in a `.guicfg` section pinned at the base of
// its MEM1 region, so those addresses do not move when the mod is rebuilt. A
// user's layout is a `06` block write over the elements they configured --
// anything left out keeps the compiled-in default.
//
// The addresses ARE version-specific: the config lives inside the mod, and the
// mod links at each revision's __ArenaLo.

import { packRGBA } from './util.js';
import { measureText, CELL } from './text.js';
import { TYPES } from './model.js';

// __ArenaLo per revision -- patches.py `arena_lo`, which is where link_mod.py
// pins `.guicfg`.
const BLOCK_BASE = { JP: 0x80426020, US: 0x80429800, EU: 0x80420d60 };

// gui_config.hxx: the private timer state comes first, the published config
// after it. Offsets within the config are append-only.
const CONFIG_OFF = 0x60;
const OFF_QFT = 0x00; // TextStyle + u16 freezeFrames + u16 pad = 32 bytes
const OFF_QFST = 0x20; // TextStyle = 28 bytes

const hex8 = (n) => (n >>> 0).toString(16).toUpperCase().padStart(8, '0');

class Bytes {
  constructor() {
    this.b = [];
  }
  u16(v) {
    this.b.push((v >> 8) & 0xff, v & 0xff);
  }
  u32(v) {
    this.b.push((v >>> 24) & 0xff, (v >>> 16) & 0xff, (v >>> 8) & 0xff, v & 0xff);
  }
}

/**
 * The background rectangle, resolved. This is upstream's getFillRectParams:
 * the four padding offsets the inspector edits are relative to the text box,
 * and it is the generator that measures the text, exactly as the mod expects.
 */
export function bgRect(el, region) {
  const { width, height } = measureText(TYPES[el.kind].previewText(el, region), region);
  const s = el.fontSize;
  return [
    el.x - el.bgLeft,
    el.y - s - el.bgTop,
    el.x + Math.ceil((width * s) / CELL) + el.bgRight,
    el.y - s + Math.ceil((height * s) / CELL) + el.bgBot,
  ];
}

/** SusamuneTextStyle -- 28 bytes. */
function writeStyle(w, el, region) {
  const gradient = el.fgRGB2 != null && el.fgA2 != null;
  w.u16(el.x);
  w.u16(el.y);
  for (const v of bgRect(el, region)) w.u16(v);
  w.u16(el.fontSize);
  w.u16(0); // pad
  w.u32(packRGBA(el.fgRGB, el.fgA));
  // fgBottom mirrors fgTop when the gradient is off, so the mod never has to
  // branch on a flag to pick a colour.
  w.u32(gradient ? packRGBA(el.fgRGB2, el.fgA2) : packRGBA(el.fgRGB, el.fgA));
  w.u32(packRGBA(el.bgRGB, el.bgA));
}

/** One `06` block write, zero-padded to a multiple of 8. */
function blockWrite(addr, bytes) {
  const p = bytes.slice();
  while (p.length % 8) p.push(0);
  const lines = [`06${hex8(addr & 0x01ffffff).slice(2)} ${hex8(bytes.length)}`];
  for (let i = 0; i < p.length; i += 8) {
    const w = (o) =>
      hex8(((p[i + o] << 24) | (p[i + o + 1] << 16) | (p[i + o + 2] << 8) | p[i + o + 3]) >>> 0);
    lines.push(`${w(0)} ${w(4)}`);
  }
  return lines;
}

/**
 * @returns {{lines: string[], dropped: string[]}} `lines` is empty when nothing
 * this emitter understands is on the canvas -- an empty code would only take up
 * room in the user's .gct.
 */
export function buildGecko(elements, region) {
  const base = BLOCK_BASE[region];
  if (base == null) throw new Error(`no config address for region ${region}`);
  const cfg = base + CONFIG_OFF;

  const lines = [];
  const dropped = [];

  for (const el of elements) {
    if (el.kind === 'qft') {
      const w = new Bytes();
      writeStyle(w, el, region);
      w.u16(el.freezeDuration);
      w.u16(0);
      lines.push(...blockWrite(cfg + OFF_QFT, w.b));
    } else if (el.kind === 'qfst') {
      const w = new Bytes();
      writeStyle(w, el, region);
      lines.push(...blockWrite(cfg + OFF_QFST, w.b));
    } else {
      // TODO: the mod does not render Customized Display, Attempt Counter,
      // Pattern Selector or the Controller Input Display yet, and has no config
      // block reserved for them. They stay editable so a layout survives, but
      // nothing is emitted.
      dropped.push(el.kind);
    }
  }

  return { lines, dropped };
}
