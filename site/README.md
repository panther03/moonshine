# Susamune GUI Configurator

A static page for laying out susamune's on-screen UI — position display cells,
the quarterframe timers, the attempt counter, the pattern selector and the
controller input display — and exporting them as `susamune_gui.bin`.

It replaces the per-code configuration forms on the gct-generator site with a
single canvas you drag things around on. Users save the resulting file to the
root of their SD card next to `susamune.ini`.

## Running it

No build step and no dependencies. It does have to be **served over HTTP**,
including locally: it is built from ES modules, and browsers refuse to load
those from a `file://` path. Opening `index.html` by double-clicking it gives a
page that looks right but is completely inert — the page detects that and says
so rather than leaving you clicking dead buttons.

```sh
python site/serve.py        # serves and opens a browser; optional port argument
```

or equivalently `cd site && python -m http.server 8000`.

Deploying is a copy: point GitHub Pages at this directory.

`selftest.html` is a dependency-free regression check for the parts with a
defined right answer — the markup parser and the blob layout. Open it after
touching `format.js` or `serialize.js`; every line should read `PASS`, and it
prints a hexdump you can check against FORMAT.md by hand.

## What it produces

One region-independent binary blob. **[FORMAT.md](FORMAT.md) is the spec** and
the contract the mod-side reader implements — change one, change the other.

The region selector affects only the preview: it picks which of the game's font
sheets the canvas measures and draws text with. Nothing region-specific is
serialised.

## Layout

| path | |
|---|---|
| `js/model.js` | element catalogue, defaults, and the ready-made display presets |
| `js/fields.js` | the values a display cell can print, and their format-blob ids |
| `js/format.js` | `<field\|format\|preview>` markup → printf string + field ids |
| `js/serialize.js` | the blob writer; mirrors FORMAT.md |
| `js/preview.js` | canvas rendering and element bounds |
| `js/text.js` | text measurement against the game's font metrics |
| `js/app.js` | state, canvas interaction, element list, inspector, export |
| `data/`, `img/` | font metrics, font sheets and the backdrop |

## Provenance

The font sheets, character metrics and the backdrop come from
[gct-generator](https://github.com/sup39/gct-generator) (sup39), as do the
default positions and the display-cell markup syntax — an existing config pastes
in unchanged.

The coordinate handling deliberately differs: everything here lives in one
space, the game 2D space the upstream inputs declare (`x` 0..600, `y` 16..464).
Upstream's controller code stores `y - 16` and previews `x` in a 640-wide space;
carrying a second convention into a drag-and-drop editor would mean two elements
dropped on the same spot did not land on the same spot. See the "Coordinates"
section of FORMAT.md.
