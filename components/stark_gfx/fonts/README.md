# stark_gfx fonts — provenance and licence

## `gfx_font_mono16` (`font_mono16.c`)

| | |
| --- | --- |
| Source font | X.Org **misc-fixed 8x13**, XLFD `-Misc-Fixed-Medium-R-Normal--13-120-75-75-C-80-ISO10646-1` (X.Org `font-misc-misc`) |
| Licence | **Public domain.** The font's own `COPYRIGHT` property reads `"Public domain font.  Share and enjoy."`, and X.Org's `font-misc-misc/COPYING` says the same (quoted in Debian/Ubuntu `xfonts-base`'s copyright file). The property is kept in the committed BDF. |
| Obtained from | Ubuntu 24.04 package `xfonts-base` `1:1.0.5+nmu1`, file `/usr/share/fonts/X11/misc/8x13.pcf.gz` — sha256 `4b429fdefcf6ff1587469bf9aded01cbf3866ebb83661ec569224c398b93bcbc` |
| Converted with | `pcf2bdf` 1.07 (Ubuntu 24.04) → full BDF, 3703 glyphs — sha256 `0ff97dff1f77ced0ad5e6dc909e1adf44403a30d1e27838dfdafc51418c79a6d` |
| Committed source | `misc-fixed-8x13-ascii.bdf` — the U+0020..U+007E subset of that BDF, header and properties unchanged |
| Cell | 8×16: the 8×13 glyphs (ascent 11, descent 2) sit with their baseline on cell row 13, i.e. 2 blank rows above and 1 below. Fallback glyph `?` (U+003F). |

Why not the `8x16` font in the same package: it is Sony's
(`Copyright (c) 1987, 1988 Sony Corp.`), permissively licensed but **not** public
domain, which TASKS.md STARK-0014 requires.

Reproduce (from the repository root):

```bash
zcat /usr/share/fonts/X11/misc/8x13.pcf.gz > 8x13.pcf && pcf2bdf -o misc-fixed-8x13.bdf 8x13.pcf
python3 tools/fontconv.py subset misc-fixed-8x13.bdf components/stark_gfx/fonts/misc-fixed-8x13-ascii.bdf --first 0x20 --last 0x7E
python3 tools/fontconv.py generate components/stark_gfx/fonts/misc-fixed-8x13-ascii.bdf components/stark_gfx/fonts/font_mono16.c --name gfx_font_mono16 --cell 8x16 --baseline 13 --first 0x20 --last 0x7E --fallback 0x3F
```

`scripts/check.sh` regenerates `font_mono16.c` from the committed subset, and re-runs the
subset step on the committed subset (it must be a fixed point), into a temporary
directory; it fails unless both are byte-identical to the committed files. Latin-1 / Turkish
coverage (V0.1) is a wider `--last` / subset from the same public-domain source.
