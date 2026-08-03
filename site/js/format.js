// Compiles a Customized Display cell's markup into the two things the blob
// stores: a ready-to-use printf format string and the ordered list of field ids
// that feed it.
//
// Markup is `<field|format|preview>`, the same syntax the upstream generator
// uses, so an existing config pastes in unchanged:
//
//   X Pos <x|.0|39.39>   ->  fmt "X Pos %.0f", fields [1]
//
// `format` and `preview` are optional; the field's defaults fill in. Literal
// text is copied through with `%` escaped to `%%`, which is why the mod can hand
// the string straight to vsnprintf without inspecting it.

import { FIELD_BY_NAME, DTYPE_INFO } from './fields.js';

const MARKUP = /<(.*?)>/g;

/** Normalise a float spec (`.2`, `%6.2f`, `.3e`) and render its placeholder. */
function floatSpec(spec, value) {
  const m = spec.trim().match(/^%?(\d*)\.?(\d*)([eEf]?)$/);
  const pad = m?.[1] || '';
  const digits = +(m?.[2] || 0);
  const suffix = m?.[3] || 'f';
  let text = suffix === 'f' ? value.toFixed(digits) : value.toExponential(digits);
  if (suffix === 'E') text = text.toUpperCase();
  return { fmt: `%${pad}.${digits}${suffix}`, text, pad };
}

/** Normalise an integer spec (`04x`, `%d`) and render its placeholder. */
function intSpec(spec, value, dtype) {
  const { prefix, mask, signed } = DTYPE_INFO[dtype];
  const m = spec.trim().match(/^%?(\d*)h{0,2}([dioxXu]?)$/);
  const pad = m?.[1] || '';
  const conv = m?.[2] || (signed ? 'd' : 'u');

  let v = value & mask;
  let text;
  if (conv === 'd' || conv === 'i') {
    // Reinterpret as signed within the field's width.
    if (v > mask >>> 1) v -= mask + 1;
    text = v.toString(10);
  } else if (conv === 'o') text = (v >>> 0).toString(8);
  else if (conv === 'x') text = (v >>> 0).toString(16);
  else if (conv === 'X') text = (v >>> 0).toString(16).toUpperCase();
  else text = (v >>> 0).toString(10);

  return { fmt: `%${pad}${prefix}${conv}`, text, pad };
}

/**
 * @returns {{format: string, preview: string, fieldIds: number[], errors: string[]}}
 */
export function parseFormat(input, region) {
  let format = '';
  let preview = '';
  const fieldIds = [];
  const errors = [];

  let cursor = 0;
  let m;
  MARKUP.lastIndex = 0;
  while ((m = MARKUP.exec(input))) {
    const literal = input.slice(cursor, m.index);
    preview += literal;
    format += literal.replace(/%/g, '%%');
    cursor = m.index + m[0].length;

    const [rawName, rawFmt = '', rawPreview = ''] = m[1].split('|');
    const field = FIELD_BY_NAME[rawName.trim().toLowerCase()];
    if (!field) {
      // Unknown field: leave the markup visible so the mistake is obvious
      // rather than silently dropping the user's text.
      errors.push(`Unknown field "${rawName.trim()}"`);
      preview += m[0];
      format += m[0].replace(/%/g, '%%');
      continue;
    }

    if (field.dtype === 'glyph') {
      format += field.fmt;
      preview += field.preview(region);
    } else if (field.dtype === 'float') {
      const value = rawPreview && isFinite(+rawPreview) ? +rawPreview : field.preview;
      const { fmt, text, pad } = floatSpec(rawFmt || field.fmt, value);
      format += fmt;
      preview += text.padStart(+pad || 0, pad[0] === '0' ? '0' : ' ');
    } else {
      const value = rawPreview && isFinite(+rawPreview) ? +rawPreview : field.preview;
      const { fmt, text, pad } = intSpec(rawFmt || field.fmt, value, field.dtype);
      format += fmt;
      preview += text.padStart(+pad || 0, pad[0] === '0' ? '0' : ' ');
    }
    fieldIds.push(field.id);
  }

  const tail = input.slice(cursor);
  preview += tail;
  format += tail.replace(/%/g, '%%');

  return { format, preview, fieldIds, errors };
}
