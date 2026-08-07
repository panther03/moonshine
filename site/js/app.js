// Wiring: state, the canvas, the element list, the inspector, and export.

import { TYPES, PRESETS, makeElement, reserveUids } from './model.js';
import { loadRegion } from './text.js';
import { buildElement, boundsOf, toStage, STAGE_W, STAGE_H, Y_ORIGIN } from './preview.js';
import { parseFormat } from './format.js';
import { FIELDS } from './fields.js';
import { buildGecko } from './gecko.js';
import { rgbToHex, hexToRgb, clamp } from './util.js';

const LS_KEY = 'susamune/gui-config';

const state = {
  region: 'US',
  elements: [],
  selected: null, // uid
};

const $ = (id) => document.getElementById(id);
const dom = {
  stage: $('stage'),
  elements: $('elements'),
  selection: $('selection'),
  layers: $('layers'),
  layersEmpty: $('layers-empty'),
  inspector: $('inspector'),
  inspectorTitle: $('inspector-title'),
  status: $('status'),
  region: $('region'),
  addMenu: $('add-menu'),
};

/** Tiny DOM builder. `props` may carry `on<Event>` handlers and `style` objects. */
function h(tag, props = {}, ...children) {
  const n = document.createElement(tag);
  for (const [k, v] of Object.entries(props)) {
    if (v == null || v === false) continue;
    if (k === 'class') n.className = v;
    else if (k === 'style') Object.assign(n.style, v);
    else if (k.startsWith('on')) n.addEventListener(k.slice(2).toLowerCase(), v);
    else if (k in n) n[k] = v;
    else n.setAttribute(k, v);
  }
  for (const c of children.flat()) {
    if (c != null && c !== false) n.append(c);
  }
  return n;
}

const selectedElement = () => state.elements.find((e) => e.uid === state.selected) ?? null;

// ---------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------

function save() {
  try {
    localStorage.setItem(
      LS_KEY,
      JSON.stringify({ region: state.region, elements: state.elements }),
    );
  } catch {
    /* private mode: the config just will not survive a reload */
  }
}

function load() {
  try {
    const raw = JSON.parse(localStorage.getItem(LS_KEY) ?? 'null');
    if (!raw || !Array.isArray(raw.elements)) return false;
    // Drop anything whose kind no longer exists, and fill in fields added since
    // the config was written, so an old save never renders half-configured.
    state.elements = raw.elements
      .filter((e) => TYPES[e.kind])
      .map((e) => ({ ...TYPES[e.kind].defaults, ...e }));
    if (['US', 'JP', 'EU'].includes(raw.region)) state.region = raw.region;
    reserveUids(state.elements);
    return true;
  } catch {
    return false;
  }
}

// ---------------------------------------------------------------------
// Canvas
// ---------------------------------------------------------------------

function renderCanvas() {
  dom.elements.replaceChildren(
    ...state.elements.map((el) => buildElement(el, state.region)),
  );
  renderSelection();
}

function renderSelection() {
  const el = selectedElement();
  dom.selection.replaceChildren();
  if (!el) {
    dom.selection.hidden = true;
    return;
  }
  const b = boundsOf(el, state.region);
  dom.selection.hidden = false;
  dom.selection.append(
    h('div', {
      class: 'selection-box',
      style: {
        left: `${Math.round(b.left) - 1}px`,
        top: `${Math.round(b.top) - 1}px`,
        width: `${Math.max(2, Math.round(b.width)) + 2}px`,
        height: `${Math.max(2, Math.round(b.height)) + 2}px`,
      },
    }),
  );
}

// Dragging moves the element and syncs the inspector's position inputs in
// place; rebuilding the inspector mid-drag would fight the pointer.
let posInputs = null;

function beginDrag(event) {
  const node = event.target.closest('.element');
  if (!node) {
    select(null);
    return;
  }
  const el = state.elements.find((e) => e.uid === +node.dataset.uid);
  if (!el) return;

  if (el.uid !== state.selected) select(el.uid);
  dom.stage.focus();

  const startX = event.clientX;
  const startY = event.clientY;
  const originX = el.x;
  const originY = el.y;
  node.classList.add('dragging');
  dom.stage.setPointerCapture(event.pointerId);

  const move = (ev) => {
    el.x = clamp(Math.round(originX + (ev.clientX - startX)), -400, STAGE_W + 400);
    el.y = clamp(Math.round(originY + (ev.clientY - startY)), Y_ORIGIN - 400, STAGE_H + 400);
    const { left, top } = toStage(el.x, el.y);
    node.style.transform = `translate(${left}px, ${top}px)`;
    renderSelection();
    syncPosInputs(el);
  };
  const up = (ev) => {
    dom.stage.releasePointerCapture(ev.pointerId);
    dom.stage.removeEventListener('pointermove', move);
    dom.stage.removeEventListener('pointerup', up);
    node.classList.remove('dragging');
    // The background box tracks the element origin, so it has to be rebuilt now
    // that the drag is settled.
    renderCanvas();
    renderLayers();
    save();
  };
  dom.stage.addEventListener('pointermove', move);
  dom.stage.addEventListener('pointerup', up);
  event.preventDefault();
}

function syncPosInputs(el) {
  if (!posInputs) return;
  posInputs.x.value = el.x;
  posInputs.y.value = el.y;
}

function onStageKey(event) {
  const el = selectedElement();
  if (!el) return;
  if (event.key === 'Delete' || event.key === 'Backspace') {
    removeElement(el.uid);
    event.preventDefault();
    return;
  }
  const step = event.shiftKey ? 10 : 1;
  const deltas = {
    ArrowLeft: [-step, 0],
    ArrowRight: [step, 0],
    ArrowUp: [0, -step],
    ArrowDown: [0, step],
  };
  const d = deltas[event.key];
  if (!d) return;
  el.x += d[0];
  el.y += d[1];
  refresh();
  syncPosInputs(el);
  event.preventDefault();
}

// ---------------------------------------------------------------------
// Element list
// ---------------------------------------------------------------------

/** One-line description for a layer row. */
function layerSubtitle(el) {
  if (el.kind === 'display') {
    const first = parseFormat(el.fmt, state.region).preview.split('\n')[0].trim();
    return first || 'blank';
  }
  return `${el.x}, ${el.y}`;
}

function renderLayers() {
  dom.layersEmpty.hidden = state.elements.length > 0;
  dom.layers.replaceChildren(
    // Newest on top, matching how a layers pane usually reads.
    ...[...state.elements].reverse().map((el) => {
      const type = TYPES[el.kind];
      return h(
        'li',
        {
          class: `layer${el.uid === state.selected ? ' selected' : ''}`,
          onclick: () => select(el.uid),
        },
        h(
          'span',
          { class: 'layer-name' },
          type.label,
          h('span', { class: 'layer-sub', textContent: ` — ${layerSubtitle(el)}` }),
        ),
        h('button', {
          class: 'layer-remove',
          title: 'Remove',
          textContent: '×',
          onclick: (ev) => {
            ev.stopPropagation();
            removeElement(el.uid);
          },
        }),
      );
    }),
  );
}

function addElement(kind, overrides) {
  const el = makeElement(kind, overrides);
  state.elements.push(el);
  state.selected = el.uid;
  refresh({ inspector: true });
}

function removeElement(uid) {
  state.elements = state.elements.filter((e) => e.uid !== uid);
  if (state.selected === uid) state.selected = null;
  refresh({ inspector: true });
}

function select(uid) {
  if (state.selected === uid) return;
  state.selected = uid;
  renderLayers();
  renderSelection();
  renderInspector();
}

// ---------------------------------------------------------------------
// Add menu
// ---------------------------------------------------------------------

function buildAddMenu() {
  const used = new Set(state.elements.map((e) => e.kind));
  const items = [h('div', { class: 'menu-label', textContent: 'Customized Display' })];

  items.push(
    h('button', {
      textContent: 'Blank cell',
      onclick: () => closeMenuThen(() => addElement('display')),
    }),
  );
  for (const preset of Object.values(PRESETS)) {
    items.push(
      h('button', {
        textContent: preset.label,
        onclick: () => closeMenuThen(() => addElement('display', preset.config)),
      }),
    );
  }

  items.push(h('hr'), h('div', { class: 'menu-label', textContent: 'Single elements' }));
  for (const [kind, type] of Object.entries(TYPES)) {
    if (type.multiple) continue;
    const taken = used.has(kind);
    items.push(
      h('button', {
        textContent: type.label,
        disabled: taken,
        title: taken ? 'Already on the canvas' : '',
        onclick: () => closeMenuThen(() => addElement(kind)),
      }),
    );
  }

  dom.addMenu.replaceChildren(...items);
}

const closeMenu = () => {
  dom.addMenu.hidden = true;
};
const closeMenuThen = (fn) => {
  closeMenu();
  fn();
};

// ---------------------------------------------------------------------
// Inspector
// ---------------------------------------------------------------------

/** Number input bound to `el[key]`, re-rendering the canvas as it changes. */
function numberInput(el, key, { min, max, onInput } = {}) {
  return h('input', {
    type: 'number',
    value: el[key],
    min,
    max,
    oninput: (ev) => {
      const v = ev.target.value === '' ? 0 : Number(ev.target.value);
      if (!isFinite(v)) return;
      el[key] = clamp(Math.round(v), min ?? -32768, max ?? 32767);
      onInput?.(el[key]);
      refresh();
    },
  });
}

/** Colour swatch + 0-255 alpha, the pair every colour in the format uses. */
function colorRow(el, label, rgbKey, alphaKey) {
  const alpha = h('input', {
    type: 'range',
    min: 0,
    max: 255,
    value: el[alphaKey] ?? 255,
    oninput: (ev) => {
      el[alphaKey] = Number(ev.target.value);
      pct.textContent = `${((el[alphaKey] / 2.55) | 0)}%`;
      refresh();
    },
  });
  const pct = h('span', {
    class: 'sub',
    textContent: `${(((el[alphaKey] ?? 255) / 2.55) | 0)}%`,
  });
  return h(
    'div',
    { class: 'row' },
    h('label', { textContent: label }),
    h('input', {
      type: 'color',
      value: rgbToHex(el[rgbKey] ?? 0),
      oninput: (ev) => {
        el[rgbKey] = hexToRgb(ev.target.value);
        refresh();
      },
    }),
    alpha,
    pct,
  );
}

function styleGroups(el) {
  const groups = [];

  posInputs = { x: numberInput(el, 'x'), y: numberInput(el, 'y') };
  groups.push(
    h(
      'div',
      { class: 'group' },
      h('h3', { textContent: 'Placement' }),
      h(
        'div',
        { class: 'row' },
        h('label', { textContent: 'Position' }),
        posInputs.x,
        posInputs.y,
      ),
      h(
        'div',
        { class: 'row' },
        h('label', { textContent: 'Font size' }),
        numberInput(el, 'fontSize', { min: 0, max: 255 }),
        h('span', { class: 'sub', textContent: '20 = native' }),
      ),
    ),
  );

  const gradientOn = el.fgRGB2 != null && el.fgA2 != null;
  const fgRows = [
    h('h3', { textContent: 'Text colour' }),
    colorRow(el, gradientOn ? 'Top' : 'Colour', 'fgRGB', 'fgA'),
    h(
      'div',
      { class: 'row' },
      h('label', { textContent: 'Gradient' }),
      h('input', {
        type: 'checkbox',
        checked: gradientOn,
        onchange: (ev) => {
          if (ev.target.checked) {
            el.fgRGB2 = el.fgRGB;
            el.fgA2 = el.fgA;
          } else {
            el.fgRGB2 = null;
            el.fgA2 = null;
          }
          refresh({ inspector: true });
        },
      }),
    ),
  ];
  if (gradientOn) fgRows.splice(2, 0, colorRow(el, 'Bottom', 'fgRGB2', 'fgA2'));
  groups.push(h('div', { class: 'group' }, ...fgRows));

  groups.push(
    h(
      'div',
      { class: 'group' },
      h('h3', { textContent: 'Background' }),
      colorRow(el, 'Colour', 'bgRGB', 'bgA'),
      h(
        'div',
        { class: 'pad-grid' },
        ...[
          ['bgLeft', 'Left'],
          ['bgRight', 'Right'],
          ['bgTop', 'Top'],
          ['bgBot', 'Bottom'],
        ].map(([key, label]) =>
          h(
            'label',
            {},
            label,
            numberInput(el, key, { min: -128, max: 127 }),
          ),
        ),
      ),
      h('p', {
        class: 'note',
        textContent:
          'Padding grows the box outward from the text. Alpha 0 draws no background at all.',
      }),
    ),
  );

  return groups;
}

function extrasGroup(el, type) {
  if (!type.extras) return null;
  return h(
    'div',
    { class: 'group' },
    h('h3', { textContent: 'Behaviour' }),
    ...type.extras.map((extra) => {
      const hint = h('span', { class: 'sub', textContent: extra.hint(el[extra.key]) });
      return h(
        'div',
        { class: 'row' },
        h('label', { textContent: extra.label }),
        numberInput(el, extra.key, {
          min: extra.min,
          max: extra.max,
          onInput: (v) => {
            hint.textContent = extra.hint(v);
          },
        }),
        h('span', { class: 'sub', textContent: extra.unit }),
        hint,
      );
    }),
  );
}

function formatGroup(el) {
  const warn = h('p', { class: 'warn' });
  const textarea = h('textarea', {
    value: el.fmt,
    spellcheck: false,
    placeholder: 'X Pos <x|.0>',
    oninput: (ev) => {
      el.fmt = ev.target.value;
      warn.textContent = parseFormat(el.fmt, state.region).errors.join(' · ');
      refresh();
    },
  });
  warn.textContent = parseFormat(el.fmt, state.region).errors.join(' · ');

  const chips = h(
    'div',
    { class: 'chips' },
    ...FIELDS.map((f) =>
      h('button', {
        class: 'chip',
        textContent: f.name,
        title: f.label,
        onclick: () => {
          // Insert at the caret so a field can be dropped mid-line.
          const at = textarea.selectionStart ?? el.fmt.length;
          const end = textarea.selectionEnd ?? at;
          const token = `<${f.name}>`;
          el.fmt = el.fmt.slice(0, at) + token + el.fmt.slice(end);
          textarea.value = el.fmt;
          textarea.focus();
          textarea.setSelectionRange(at + token.length, at + token.length);
          warn.textContent = parseFormat(el.fmt, state.region).errors.join(' · ');
          refresh();
        },
      }),
    ),
  );

  return h(
    'div',
    { class: 'group' },
    h('h3', { textContent: 'Contents' }),
    textarea,
    chips,
    h('p', {
      class: 'note',
      textContent:
        'Syntax: <field>, <field|format>, <field|format|preview>. Format is a printf spec (.2, 04x); preview only changes the placeholder shown here.',
    }),
    warn,
  );
}

function controllerGroups(el) {
  posInputs = { x: numberInput(el, 'x'), y: numberInput(el, 'y') };
  return [
    h(
      'div',
      { class: 'group' },
      h('h3', { textContent: 'Placement' }),
      h(
        'div',
        { class: 'row' },
        h('label', { textContent: 'Position' }),
        posInputs.x,
        posInputs.y,
      ),
      h(
        'div',
        { class: 'row' },
        h('label', { textContent: 'Height' }),
        numberInput(el, 'height', { min: 1, max: 448 }),
        h('span', { class: 'sub', textContent: '120 = native' }),
      ),
      h(
        'div',
        { class: 'row' },
        h('label', { textContent: 'Line width' }),
        numberInput(el, 'lineWidth', { min: 1, max: 255 }),
      ),
    ),
    h('div', { class: 'group' }, h('h3', { textContent: 'Background' }), colorRow(el, 'Colour', 'bgRGB', 'bgA')),
    h(
      'div',
      { class: 'group' },
      h('p', {
        class: 'note',
        textContent:
          'Button, stick and trigger geometry is fixed — it is baked into the mod, as it is in the original code.',
      }),
    ),
  ];
}

function renderInspector() {
  posInputs = null;
  const el = selectedElement();
  if (!el) {
    dom.inspectorTitle.textContent = 'Inspector';
    dom.inspector.replaceChildren(
      h('p', { class: 'empty', textContent: 'Select an element to edit it.' }),
    );
    return;
  }
  const type = TYPES[el.kind];
  dom.inspectorTitle.textContent = type.label;

  const groups =
    el.kind === 'controller'
      ? controllerGroups(el)
      : [
          el.kind === 'display' ? formatGroup(el) : null,
          ...styleGroups(el),
          extrasGroup(el, type),
        ];

  dom.inspector.replaceChildren(...groups.filter(Boolean));
}

// ---------------------------------------------------------------------
// Refresh + export
// ---------------------------------------------------------------------

function refresh({ inspector = false } = {}) {
  renderCanvas();
  renderLayers();
  if (inspector) renderInspector();
  buildAddMenu();
  save();
}

function showExport() {
  let lines, dropped;
  try {
    ({ lines, dropped } = buildGecko(state.elements, state.region));
  } catch (err) {
    dom.status.textContent = `Could not build the code: ${err.message}`;
    return;
  }
  dom.status.textContent = '';

  const label = { US: 'GMSE01 (US)', JP: 'GMSJ01 (JP)', EU: 'GMSP01 (PAL)' }[state.region];
  const text = lines.join('\n');

  if (!lines.length) {
    // No code at all rather than an empty one: the mod's own defaults already
    // are the unconfigured layout, and a runner's .gct has finite room.
    $('export-summary').textContent =
      'Nothing to emit — add a Quarterframe Timer or QF Section Timer to the canvas.';
  } else {
    $('export-summary').textContent =
      `${lines.length} lines, for ${label}. ` +
      'The addresses are inside the mod, so this code is for that revision only.';
  }

  const note = dropped.length
    ? `Not included: ${[...new Set(dropped)].join(', ')} — the mod does not render ` +
      'these yet, so nothing is emitted for them. Your layout is still saved.'
    : 'Add this to the .gct you load alongside the mod.';
  $('dialog-note').textContent = note;

  $('export-dump').textContent = text || '(empty)';

  const copy = $('copy-code');
  copy.disabled = !lines.length;
  copy.onclick = async (ev) => {
    await navigator.clipboard.writeText(text);
    ev.target.textContent = 'Copied';
    setTimeout(() => (ev.target.textContent = 'Copy code'), 1200);
  };

  $('export-dialog').showModal();
}

// ---------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------

async function setRegion(region) {
  state.region = region;
  dom.region.value = region;
  await loadRegion(region);
  refresh({ inspector: true });
}

async function main() {
  if (!load()) {
    // A first-time visitor gets the layout most runners start from rather than
    // an empty canvas.
    state.elements = [makeElement('display', PRESETS.PAS.config), makeElement('qft')];
  }
  // Land on something editable rather than an empty inspector.
  state.selected = state.elements[0]?.uid ?? null;

  dom.stage.addEventListener('pointerdown', beginDrag);
  dom.stage.addEventListener('keydown', onStageKey);
  dom.region.addEventListener('change', (ev) => setRegion(ev.target.value));
  $('export').addEventListener('click', showExport);
  $('close-dialog').addEventListener('click', () => $('export-dialog').close());
  $('reset').addEventListener('click', () => {
    if (!confirm('Discard the current layout and start from an empty canvas?')) return;
    state.elements = [];
    state.selected = null;
    refresh({ inspector: true });
  });

  $('add').addEventListener('click', (ev) => {
    ev.stopPropagation();
    dom.addMenu.hidden = !dom.addMenu.hidden;
  });
  document.addEventListener('click', (ev) => {
    if (!ev.target.closest('.add-wrap')) closeMenu();
  });

  await setRegion(state.region);
  // Tells the boot-check in index.html that the modules loaded; without it the
  // page would sit there looking correct but inert (see the file:// banner).
  window.__susamuneBooted = true;
}

main();
