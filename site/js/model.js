// Element catalogue: what can go on the canvas, what each one stores, and the
// defaults. Defaults are the upstream generator's, so a config built here lands
// where a runner's muscle memory expects it.

import { parseFormat } from './format.js';

/** Shared styling block -- serialised as TextStyle (FORMAT.md). */
const STYLE = {
  x: 16,
  y: 200,
  fontSize: 20,
  fgRGB: 0xffffff,
  fgA: 0xff,
  fgRGB2: null, // non-null enables the vertical gradient
  fgA2: null,
  bgRGB: 0x000000,
  bgA: 0,
  bgLeft: 0,
  bgRight: 0,
  bgTop: 0,
  bgBot: 0,
};

// The section timer's placeholder is deliberately 16 lines: its box has to be
// sized for a full run's worth of splits, not for one.
const QFST_PREVIEW = [
  ' 0.426', ' 0.427', ' 0.428', ' 1.515', ' 3.117', '39.000', ' 9.999', '11.111',
  '22.222', '33.333', '44.444', '55.555', '66.666', '77.777', '88.888', '99.999',
].join('\n');

export const TYPES = {
  display: {
    typeId: 0x01,
    label: 'Customized Display',
    multiple: true,
    defaults: { ...STYLE, fmt: '' },
    previewText: (el, region) => parseFormat(el.fmt, region).preview,
  },
  qft: {
    typeId: 0x02,
    label: 'Quarterframe Timer',
    multiple: false,
    defaults: {
      ...STYLE,
      x: 16,
      y: 456,
      bgA: 0x80,
      bgRight: 2,
      bgTop: 2,
      freezeDuration: 30,
    },
    previewText: () => '0:00.000',
    extras: [
      {
        key: 'freezeDuration',
        label: 'Freeze duration',
        min: 0,
        max: 32767,
        unit: 'frames',
        hint: (v) => `${((v * 1001) / 30000).toFixed(2)}s`,
      },
    ],
  },
  qfst: {
    typeId: 0x03,
    label: 'QF Section Timer',
    multiple: false,
    defaults: {
      ...STYLE,
      x: 533,
      y: 150,
      fontSize: 13,
      bgA: 0x40,
      bgLeft: 4,
      bgRight: 3,
      bgTop: 4,
      bgBot: 2,
    },
    previewText: () => QFST_PREVIEW,
  },
  attempts: {
    typeId: 0x04,
    label: 'Attempt Counter',
    multiple: false,
    defaults: {
      ...STYLE,
      x: 152,
      y: 125,
      fontSize: 32,
      fgRGB: 0xffff99,
      bgA: 0x40,
      bgLeft: 4,
      bgRight: 6,
      bgTop: 4,
      bgBot: 3,
      duration: 60,
    },
    previewText: () => '88\n99',
    extras: [
      {
        key: 'duration',
        label: 'Display duration',
        min: 0,
        max: 32767,
        unit: 'frames',
        hint: (v) => `${(v / 60).toFixed(2)}s`,
      },
    ],
  },
  pattern: {
    typeId: 0x05,
    label: 'Pattern Selector',
    multiple: false,
    defaults: {
      ...STYLE,
      x: 498,
      y: 462,
      fontSize: 14,
      bgA: 128,
      bgLeft: 2,
      bgRight: 4,
      bgTop: 2,
      bgBot: 2,
    },
    previewText: () => '#0 0 0',
  },
  controller: {
    typeId: 0x06,
    label: 'Controller Input',
    multiple: false,
    // Only placement and the backdrop are configurable; button geometry and
    // colours stay baked into the mod, as they are baked upstream.
    defaults: { x: 16, y: 314, height: 120, lineWidth: 20, bgRGB: 0x000000, bgA: 0x7f },
  },
};

// Ready-made Customized Display cells, matching the upstream generator's
// buttons so the familiar presets are one click away.
export const PRESETS = {
  PAS: {
    label: 'Position / Angle / Speed',
    config: {
      x: 16,
      y: 200,
      fmt: [
        'X Pos <x|.0|39.39>',
        'Y Pos <y|.0|1207.39>',
        'Z Pos <z|.0|-4193.6>',
        'Angle <angle||65535>',
        'H Spd <hspd|.2|15.15>',
        'V Spd <vspd|.2|-31.17>',
      ].join('\n'),
    },
  },
  speed: {
    label: 'Speed only',
    config: {
      x: 16,
      y: 240,
      fmt: ['H Spd <hspd|.2|15.15>', 'V Spd <vspd|.2|-31.17>'].join('\n'),
    },
  },
  detailed: {
    label: 'Detailed',
    config: {
      x: 16,
      y: 192,
      fontSize: 16,
      fmt: [
        'X <x|.0|39.39>',
        'Y <y|.0|1207.39>',
        'Z <z|.0|-4193.6>',
        'A <angle||65535>',
        'C <cangle||9>',
        'H <hspd|.2|15.15>',
        'V <vspd|.2|-31.17>',
        'QF <qf||0>',
        'I <invinc||30>',
        'G <goop||36368>',
        'Spin <spin>',
      ].join('\n'),
    },
  },
  rect: {
    label: 'Blank rectangle',
    // fontSize 0 with no text: the bg offsets alone define the box, which is
    // how the upstream "rect" preset builds a dimming overlay.
    config: { x: 32, y: 48, fontSize: 0, fmt: '', bgRight: 536, bgBot: 384, bgA: 0x7f },
  },
};

let nextUid = 1;

export function makeElement(kind, overrides = {}) {
  return { uid: nextUid++, kind, ...TYPES[kind].defaults, ...overrides };
}

/** Rehydrating from storage must not reissue uids already in play. */
export function reserveUids(elements) {
  for (const el of elements) if (el.uid >= nextUid) nextUid = el.uid + 1;
}
