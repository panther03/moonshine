# Susamune GUI Configurator

A static page for laying out susamune's on-screen UI — position display cells,
the quarterframe timers, the attempt counter, the pattern selector and the
controller input display — and exporting them as a **Gecko code**.

It replaces the per-code configuration forms on the gct-generator site with a
single canvas you drag things around on. The mod carries its own defaults in a
config block pinned at a fixed MEM1 address; the code the page emits overwrites
the fields you changed, so users paste it into the `.gct` they already load.

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
defined right answer — the markup parser and the emitted code. Open it after
touching `format.js` or `gecko.js`; every line should read `PASS`, and it prints
the code so you can check it against FORMAT.md by hand.

## What it produces

A Gecko code for one disc revision. **[FORMAT.md](FORMAT.md) is the spec** and
the contract `include/susamune/gui_config.hxx` implements — change one, change
the other.

The version selector picks both the preview font sheet and the addresses the
code writes to: the config block lives inside the mod, and the mod links at each
revision's `__ArenaLo`.

Only the Quarterframe Timer and QF Section Timer are emitted today; the other
element types stay editable but are dropped, with a note in the export dialog.
See "Not implemented yet" in FORMAT.md.

## Layout

| path | |
|---|---|
| `js/model.js` | element catalogue, defaults, and the ready-made display presets |
| `js/fields.js` | the values a display cell can print, and their format-blob ids |
| `js/format.js` | `<field\|format\|preview>` markup → printf string + field ids |
| `js/gecko.js` | the Gecko-code emitter; mirrors FORMAT.md |
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
space, the game's 2D space (visible band `y` 16..464), with `y` as the text
baseline. Upstream's
controller code stores `y - 16` and previews `x` in a 640-wide space; carrying a
second convention into a drag-and-drop editor would mean two elements dropped on
the same spot did not land on the same spot. See the "Coordinates" section of
FORMAT.md.
