# Launcher assets

`font.zip` contains a release-time subset of the pre-V2.2 DejaVu Sans Mono
Bold font. The generator keeps every letter, mark, number, punctuation, space,
and currency mapping that the source font actually contains, plus arrows,
geometric UI glyphs, and the replacement character. It therefore preserves the
source font's text coverage without making false claims for absent scripts or
keeping unrelated mathematical, technical, and decorative symbols resident.
The pre-V2.2 source has no CJK, Thai, or Myanmar mappings. The generated font
keeps 2,373 of its 3,069 cmap entries, including its Armenian, Lao, Georgian,
and Arabic presentation-form letters. The TTF is 189,844 bytes instead of
318,392; the embedded ZIP is 117,227 bytes instead of 181,041.

Rebuild it from an unmodified upstream archive with FontTools:

```console
python scripts/subset_launcher_font.py full-font.zip launcher/loader/data/font.zip
```

The source may be either an archive containing exactly one TTF or the TTF
itself. The script preserves font identity and licence records, checks printable
ASCII and both launcher arrow glyphs, verifies every selected codepoint and
advance width, and writes a deterministic archive.

The pre-V2.2 `background.png` was a single-colour 640x480 white image. ZETA draws
that stock white field and its widescreen side bars as rectangles, avoiding a
1,228,800-byte RGBA texture. A user's `theme/background.png` is still decoded
at 1024x480 and rendered through the custom-theme path.
