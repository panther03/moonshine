// Renders elements onto the canvas as DOM.
//
// Glyphs use the same trick as the upstream preview: two stacked 20x20 divs per
// character, one showing the raw font sheet and one multiplying the chosen
// colour through the sheet used as a mask. That reproduces the game's shaded
// glyphs rather than flat-filling them, and a gradient is just a linear-gradient
// on the mask layer.
//
// Every element's root is placed with a single transform, so dragging updates
// one style property and never rebuilds glyph DOM.

import { measureText, sheetUrl, CELL } from './text.js';
import { rgba } from './util.js';
import { TYPES } from './model.js';

// Game 2D space -> canvas pixels. The visible area starts at y = 16.
export const Y_ORIGIN = 16;
export const STAGE_W = 600;
export const STAGE_H = 448;

export const toStage = (x, y) => ({ left: x, top: y - Y_ORIGIN });
export const toGame = (left, top) => ({ x: left, y: top + Y_ORIGIN });

// Baked controller geometry, carried over from the upstream code. Not editable
// here by design -- the mod bakes the same numbers.
const PAD = {
  w: 182,
  h: 120,
  buttons: [
    { id: 'A', x: 138, y: 66, r: 18, c: 0x2ee5b8bf },
    { id: 'B', x: 113, y: 89, r: 9, c: 0xff1a1abf },
    { id: 'X', x: 164, y: 50, r: 8, c: 0xeeeeeebf },
    { id: 'Y', x: 119, y: 41, r: 8, c: 0xeeeeeebf },
    { id: 'Z', x: 144, y: 34, r: 6, c: 0x9494ffbf },
    { id: 'S', x: 91, y: 64, r: 5, c: 0xeeeeeebf },
  ],
  sticks: [
    { id: 'M', x: 32, y: 52, rS: 19, cS: 0xeeeeeeef, rF: 12, cF: 0xeeeeeeef },
    { id: 'C', x: 64, y: 92, rS: 19, cS: 0xffd300ef, rF: 12, cF: 0xffd300ef },
  ],
  triggers: [
    { id: 'L', x: 12, y0: 10, y1: 18, w: 64, wa: 56 },
    { id: 'R', x: 170, y0: 10, y1: 18, w: -64, wa: -56 },
  ],
  triggerFill: 0xdfdfdfbf,
  triggerStroke: 0xeeeeeebf,
};

/** 0xRRGGBBAA -> css */
const cssRGBA = (v) => rgba((v >>> 8) & 0xffffff, v & 0xff);

const el = (tag, cls) => {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  return n;
};

/** Bounds of a text element in canvas pixels, background padding included. */
export function textBounds(element, region) {
  const text = TYPES[element.kind].previewText(element, region);
  const { width, height } = measureText(text, region);
  const scale = element.fontSize / CELL;
  const { left, top } = toStage(element.x, element.y);
  return {
    left: left - element.bgLeft,
    top: top - element.fontSize - element.bgTop,
    width: width * scale + element.bgLeft + element.bgRight,
    height: height * scale + element.bgTop + element.bgBot,
  };
}

/** Bounds of the controller element in canvas pixels. */
export function padBounds(element) {
  const scale = element.height / PAD.h;
  const { left, top } = toStage(element.x, element.y);
  return { left, top, width: PAD.w * scale, height: element.height };
}

export function boundsOf(element, region) {
  return element.kind === 'controller' ? padBounds(element) : textBounds(element, region);
}

function buildGlyphs(element, region) {
  const text = TYPES[element.kind].previewText(element, region);
  const { chars } = measureText(text, region);
  const url = `url(${sheetUrl(region)})`;

  const fg = rgba(element.fgRGB, element.fgA);
  const gradient =
    element.fgRGB2 != null && element.fgA2 != null
      ? `linear-gradient(180deg, ${fg}, ${rgba(element.fgRGB2, element.fgA2)})`
      : fg;

  const wrap = el('div', 'glyphs');
  // Scale about the top-left so the layout stays in native 20px units.
  wrap.style.transform = `translate(0px, ${-element.fontSize}px) scale(${element.fontSize / CELL})`;

  for (const { x, y, u, v } of chars) {
    const cell = el('div', 'glyph');
    cell.style.left = `${x}px`;
    cell.style.top = `${y}px`;

    const sheet = el('div', 'glyph-sheet');
    sheet.style.backgroundImage = url;
    sheet.style.backgroundPosition = `${-u}px ${-v}px`;

    const mask = el('div', 'glyph-mask');
    mask.style.maskImage = url;
    mask.style.webkitMaskImage = url;
    mask.style.maskPosition = `${-u}px ${-v}px`;
    mask.style.webkitMaskPosition = `${-u}px ${-v}px`;
    mask.style.background = gradient;

    cell.append(sheet, mask);
    wrap.append(cell);
  }
  return wrap;
}

function buildPad(element) {
  const scale = element.height / PAD.h;
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('width', PAD.w * scale);
  svg.setAttribute('height', PAD.h * scale);
  svg.setAttribute('viewBox', `0 0 ${PAD.w} ${PAD.h}`);
  svg.classList.add('pad');

  const lw = element.lineWidth / 6;
  const parts = [];

  parts.push(
    `<rect x="0" y="0" width="${PAD.w}" height="${PAD.h}" fill="${rgba(element.bgRGB, element.bgA)}"/>`,
  );
  for (const t of PAD.triggers) {
    const sx = t.w > 0 ? t.x : t.x + t.w;
    const fx = t.w > 0 ? t.x : t.x + t.wa;
    parts.push(
      `<rect x="${sx}" y="${t.y0}" width="${Math.abs(t.w)}" height="${t.y1 - t.y0}" fill="none" stroke="${cssRGBA(PAD.triggerStroke)}" stroke-width="${lw}"/>`,
      `<rect x="${fx}" y="${t.y0}" width="${Math.abs(t.wa)}" height="${t.y1 - t.y0}" fill="${cssRGBA(PAD.triggerFill)}"/>`,
    );
  }
  for (const b of PAD.buttons) {
    parts.push(
      `<circle cx="${b.x}" cy="${b.y}" r="${b.r}" fill="none" stroke="${cssRGBA(b.c)}" stroke-width="${lw}"/>`,
    );
  }
  for (const s of PAD.sticks) {
    // The stroke is an octagon (the stick gate); the fill is the stick position,
    // shown centred since there is no live input to read.
    const pts = Array.from({ length: 8 }, (_, i) => {
      const a = (Math.PI / 4) * i;
      return `${(s.x + Math.cos(a) * s.rS).toFixed(2)},${(s.y + Math.sin(a) * s.rS).toFixed(2)}`;
    }).join(' ');
    parts.push(
      `<polygon points="${pts}" fill="none" stroke="${cssRGBA(s.cS)}" stroke-width="${lw}"/>`,
      `<circle cx="${s.x}" cy="${s.y}" r="${s.rF}" fill="${cssRGBA(s.cF)}"/>`,
    );
  }

  svg.innerHTML = parts.join('');
  return svg;
}

/** Build one element's DOM. The caller positions the returned root. */
export function buildElement(element, region) {
  const root = el('div', 'element');
  root.dataset.uid = element.uid;

  if (element.kind === 'controller') {
    const { left, top, width, height } = padBounds(element);
    root.style.transform = `translate(${left}px, ${top}px)`;
    root.append(buildPad(element));
    root.style.width = `${width}px`;
    root.style.height = `${height}px`;
  } else {
    const { left, top } = toStage(element.x, element.y);
    root.style.transform = `translate(${left}px, ${top}px)`;

    if (element.bgA > 0) {
      const b = textBounds(element, region);
      const bg = el('div', 'element-bg');
      bg.style.left = `${b.left - left}px`;
      bg.style.top = `${b.top - top}px`;
      bg.style.width = `${b.width}px`;
      bg.style.height = `${b.height}px`;
      bg.style.background = rgba(element.bgRGB, element.bgA);
      root.append(bg);
    }
    if (element.fontSize > 0) root.append(buildGlyphs(element, region));
  }
  return root;
}
