// Small shared helpers: colour conversion and clamping. The byte writer, CRC and
// hex/base64 dumps went with the old susamune_gui.bin export -- the
// configurator emits a Gecko code now (js/gecko.js).

/** @param {number} rgb 0xRRGGBB */
export const rgbToHex = (rgb) => '#' + (rgb >>> 0).toString(16).padStart(6, '0');
/** @param {string} s '#rrggbb' */
export const hexToRgb = (s) => parseInt(s.slice(1), 16);

/** CSS colour from the split rgb/alpha pair the config stores. */
export const rgba = (rgb, a) =>
  `rgba(${(rgb >> 16) & 0xff}, ${(rgb >> 8) & 0xff}, ${rgb & 0xff}, ${(a & 0xff) / 255})`;

/** Pack to the RGBA8888 word the config block stores. */
export const packRGBA = (rgb, a) => (((rgb & 0xffffff) << 8) | (a & 0xff)) >>> 0;

export const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
