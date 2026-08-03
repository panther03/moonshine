// Text measurement against the game's own font metrics, so the preview lays out
// glyphs exactly where the console will. Ported from the upstream gct-generator.
//
// charInfo tables and font sheets come from that project; a sheet is a grid of
// 20x20 cells, `rowSize` wide, indexed by each glyph's `index`.

const REGIONS = {
  JP: { file: 'charInfo-JP.js', sheet: 'font-JP.png', rowSize: 24 },
  US: { file: 'charInfo-US.js', sheet: 'font-US.png', rowSize: 16 },
  EU: { file: 'charInfo-EU.js', sheet: 'font-EU.png', rowSize: 16 },
};

export const CELL = 20; // glyph cell size, and the font's native fontSize
export const LINE = 20; // line advance

const cache = {};

export async function loadRegion(region) {
  if (!cache[region]) {
    const { file } = REGIONS[region];
    cache[region] = (await import(`../data/${file}`)).default;
  }
  return cache[region];
}

export const sheetUrl = (region) => `img/${REGIONS[region].sheet}`;

/**
 * Lay out `text`, returning each glyph's position plus its uv in the sheet.
 * Kerning is applied between adjacent glyphs only, and reset at a line break --
 * matching the game.
 */
export function measureText(text, region) {
  const charInfo = cache[region];
  const { rowSize } = REGIONS[region];
  if (!charInfo) return { chars: [], width: 0, height: LINE };

  const chars = [];
  let x = 0;
  let y = 0;
  let w = 0;
  let useKerning = false;

  for (const c of text) {
    if (c === '\n') {
      useKerning = false;
      x = 0;
      y += LINE;
      continue;
    }
    const { index, kerning, width } = charInfo[c] ?? charInfo['?'];
    if (useKerning) x -= kerning;
    useKerning = true;
    chars.push({ x, y, u: (index % rowSize) * CELL, v: ((index / rowSize) | 0) * CELL });
    x += width + kerning;
    if (x > w) w = x;
  }

  return { chars, width: w, height: y + LINE };
}

/**
 * Encode a string to the game's font codepage. `fmt` in the blob is bytes, not
 * ASCII -- the JP font in particular maps punctuation to multi-byte codes.
 */
export function encodeText(text, region) {
  const charInfo = cache[region] ?? {};
  const out = [];
  for (const c of text) {
    const code = charInfo[c]?.code ?? c.charCodeAt(0);
    if (code >= 0x100) out.push((code >> 8) & 0xff, code & 0xff);
    else out.push(code & 0xff);
  }
  return out;
}
