// The values a Customized Display cell can print.
//
// `id` is the byte written into the blob's field list and is part of the file
// format: append only, never renumber (see FORMAT.md). `dtype` decides which
// printf conversion the value takes and, mod-side, whether it occupies a GPR or
// an FPR varargs slot.
//
// `preview` is the placeholder the canvas shows -- these are live game values,
// so the configurator has nothing real to display.

export const FIELDS = [
  { id: 1, name: 'x', label: 'X position', dtype: 'float', fmt: '%.0f', preview: 426.39 },
  { id: 2, name: 'y', label: 'Y position', dtype: 'float', fmt: '%.0f', preview: -427.39 },
  { id: 3, name: 'z', label: 'Z position', dtype: 'float', fmt: '%.0f', preview: 428.39 },
  { id: 4, name: 'angle', label: 'Facing angle', dtype: 'u16', fmt: '%hu', preview: 1207 },
  { id: 5, name: 'hspd', label: 'Horizontal speed', dtype: 'float', fmt: '%.2f', preview: 15.15 },
  { id: 6, name: 'vspd', label: 'Vertical speed', dtype: 'float', fmt: '%.2f', preview: -31.17 },
  { id: 7, name: 'qf', label: 'Quarterframe', dtype: 'u32', fmt: '%u', preview: 0 },
  { id: 8, name: 'cangle', label: 'Camera angle', dtype: 'u16', fmt: '%hu', preview: 9 },
  { id: 9, name: 'invinc', label: 'Invincibility frames', dtype: 'u16', fmt: '%hd', preview: 30 },
  { id: 10, name: 'goop', label: 'Goop remaining', dtype: 's32', fmt: '%d', preview: 600 },
  // Spin renders a glyph from a small table rather than a number, so its format
  // is fixed and its preview is region-dependent.
  {
    id: 11,
    name: 'spin',
    label: 'Spin input',
    dtype: 'glyph',
    fmt: '%s',
    preview: (region) => (region === 'JP' ? '＠' : '@'),
  },
];

export const FIELD_BY_NAME = Object.fromEntries(FIELDS.map((f) => [f.name, f]));

// How each dtype formats. `mask` also does the signed/unsigned wrap the game
// would do, so a preview of an out-of-range placeholder still looks right.
export const DTYPE_INFO = {
  u16: { prefix: 'h', mask: 0xffff, signed: false },
  u32: { prefix: '', mask: 0xffffffff, signed: false },
  s32: { prefix: '', mask: 0xffffffff, signed: true },
};
