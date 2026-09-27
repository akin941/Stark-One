# stark_gfx fonts — provenance and licence

Both fonts are X.Org **misc-fixed**, public domain, subset to **U+0020..U+017F**
(Basic Latin, Latin-1 Supplement, Latin Extended-A — every Turkish letter included).
The sources have no glyphs for U+007F..U+009F (DEL and the C1 controls); the subset
simply omits them and `fontconv.py generate --fill-missing` gives those code points
the fallback glyph `?`, so `gfx_font_t` stays one contiguous range and "unmapped
draws the fallback" still holds (STARK-0102).

## `gfx_font_mono16` (`font_mono16.c`) — 8×16

| | |
| --- | --- |
| Source font | X.Org **misc-fixed 8x13**, XLFD `-Misc-Fixed-Medium-R-Normal--13-120-75-75-C-80-ISO10646-1` (X.Org `font-misc-misc`) |
| Licence | **Public domain.** The font's own `COPYRIGHT` property reads `"Public domain font.  Share and enjoy."`, and X.Org's `font-misc-misc/COPYING` says the same (quoted in Debian/Ubuntu `xfonts-base`'s copyright file). The property is kept in the committed BDF. |
| Obtained from | Ubuntu 24.04 package `xfonts-base` `1:1.0.5+nmu1`, file `/usr/share/fonts/X11/misc/8x13.pcf.gz` — sha256 `4b429fdefcf6ff1587469bf9aded01cbf3866ebb83661ec569224c398b93bcbc` |
| Converted with | `pcf2bdf` 1.07 (Ubuntu 24.04) → full BDF, 3703 glyphs — sha256 `0ff97dff1f77ced0ad5e6dc909e1adf44403a30d1e27838dfdafc51418c79a6d` |
| Committed source | `misc-fixed-8x13-latin.bdf` — the U+0020..U+017F subset of that BDF (319 glyphs), header and properties unchanged |
| Cell | 8×16: the 8×13 glyphs (ascent 11, descent 2) sit with their baseline on cell row 13, i.e. 2 blank rows above and 1 below. Fallback glyph `?` (U+003F). |

Why not the `8x16` font in the same package: it is Sony's
(`Copyright (c) 1987, 1988 Sony Corp.`), permissively licensed but **not** public
domain, which TASKS.md STARK-0014 requires.

## `gfx_font_mono10` (`font_mono10.c`) — 6×10

| | |
| --- | --- |
| Source font | X.Org **misc-fixed 6x10**, XLFD `-Misc-Fixed-Medium-R-Normal--10-100-75-75-C-60-ISO10646-1` |
| Licence | **Public domain.** `COPYRIGHT` property: `"Public domain terminal emulator font.  Share and enjoy."` (kept in the committed BDF); same `font-misc-misc/COPYING` as above. |
| Obtained from | Ubuntu 24.04 package `xfonts-base` `1:1.0.5+nmu1`, file `/usr/share/fonts/X11/misc/6x10.pcf.gz` — sha256 `3818f55cfe3f945f9b309406663a01664c05c61b78827eb6bffd467da61d6e50` |
| Converted with | `pcf2bdf` 1.07 → full BDF, 1597 glyphs — sha256 `d61fc0a144077b59599d128f31de5c01a7a8a426cf2456d581ba4421af092e19` |
| Committed source | `misc-fixed-6x10-latin.bdf` — the U+0020..U+017F subset (319 glyphs), header and properties unchanged |
| Cell | 6×10: ascent 8, descent 2, baseline on cell row 8 — the glyphs fill the cell exactly. Fallback `?`. |

## Reproduce (from the repository root)

```bash
zcat /usr/share/fonts/X11/misc/8x13.pcf.gz > 8x13.pcf && pcf2bdf -o misc-fixed-8x13.bdf 8x13.pcf
zcat /usr/share/fonts/X11/misc/6x10.pcf.gz > 6x10.pcf && pcf2bdf -o misc-fixed-6x10.bdf 6x10.pcf
python3 tools/fontconv.py subset misc-fixed-8x13.bdf components/stark_gfx/fonts/misc-fixed-8x13-latin.bdf --first 0x20 --last 0x17F --allow-missing
python3 tools/fontconv.py subset misc-fixed-6x10.bdf components/stark_gfx/fonts/misc-fixed-6x10-latin.bdf --first 0x20 --last 0x17F --allow-missing
python3 tools/fontconv.py generate components/stark_gfx/fonts/misc-fixed-8x13-latin.bdf components/stark_gfx/fonts/font_mono16.c --name gfx_font_mono16 --cell 8x16 --baseline 13 --first 0x20 --last 0x17F --fallback 0x3F --fill-missing
python3 tools/fontconv.py generate components/stark_gfx/fonts/misc-fixed-6x10-latin.bdf components/stark_gfx/fonts/font_mono10.c --name gfx_font_mono10 --cell 6x10 --baseline 8 --first 0x20 --last 0x17F --fallback 0x3F --fill-missing
```

`scripts/check.sh` re-runs the subset step on each committed subset (it must be a
fixed point) and regenerates both C tables into a temporary directory; it fails
unless all four files are byte-identical to the committed ones.
