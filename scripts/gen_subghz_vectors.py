#!/usr/bin/env python3
"""
gen_subghz_vectors.py — the Sub-GHz scene catalogue (TASKS.md STARK-0603,
ADR-0022 §4) written as test/fake/fake_subghz_scenes.h.

Every scene is SPEC-DERIVED, NOT A RECORDING OF A REAL DEVICE. They are generic
modulations built from the definitions in TASKS.md STARK-0604 (pulse-width,
pulse-distance, Manchester) with round-number timings, plus two limit scenes (more
than 2048 symbols, more than 2 s). The expectations next to each scene (the symbols
a capture keeps, their FNV-1a fingerprint and the timing histogram) are computed
here, by code written independently of the firmware's (components/stark_subghz/
subghz_raw.c), so the firmware is tested against them and never against itself.

Usage:
  gen_subghz_vectors.py            write test/fake/fake_subghz_scenes.h
  gen_subghz_vectors.py --check    exit 1 if the committed file differs (CI lint)
"""
import argparse
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "test/fake/fake_subghz_scenes.h"

MAX_SYMBOLS = 2048
MAX_TOTAL_US = 2_000_000
MAX_SYMBOL_US = 1_000_000
BINS = 8
GAP_US = 12_000  # the idle time between repeats of a frame


# ---- the capture rules (TASKS.md STARK-0603) --------------------------------

def capture(symbols):
    """What a capture keeps of a stream: leading silence dropped, same-sign symbols
    merged, at most 2048 symbols and 2 s (the last symbol cut to fit)."""
    kept = []
    total = 0
    for v in symbols:
        if v == 0 or total >= MAX_TOTAL_US or len(kept) == MAX_SYMBOLS:
            continue
        mag = min(abs(v), MAX_SYMBOL_US)
        if not kept and v < 0:
            continue
        merge = bool(kept) and (kept[-1] > 0) == (v > 0)
        have = abs(kept[-1]) if merge else 0
        mag = min(mag, MAX_SYMBOL_US - have)
        if mag == 0:
            continue
        mag = min(mag, MAX_TOTAL_US - total)
        total += mag
        signed = (have + mag) * (1 if v > 0 else -1)
        if merge:
            kept[-1] = signed
        else:
            kept.append(signed)
    return kept


def fnv1a32(data):
    h = 0x811C9DC5
    for b in data:
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


def symbol_hash(symbols):
    """FNV-1a over the symbols as little-endian int32."""
    return fnv1a32(b"".join(struct.pack("<i", v) for v in symbols))


def histogram(symbols):
    """[(count, mean_us)] ascending: durations grouped by ±20 % about 1.25 x the
    shortest one not yet grouped, that is [lo, 1.5 lo]; at most 8 bins."""
    mags = [abs(v) for v in symbols]
    out = []
    done = 0
    for _ in range(BINS):
        rest = [m for m in mags if m > done]
        if not rest:
            break
        lo = min(rest)
        hi = lo + lo // 2
        members = [m for m in mags if lo <= m <= hi]
        out.append((len(members), (sum(members) * 2 + len(members)) // (2 * len(members))))
        done = hi
    return out


# ---- the scenes ----------------------------------------------------------------

def runs(halves):
    """(level, duration) halves -> alternating signed symbols, same levels merged."""
    out = []
    for high, us in halves:
        if us == 0:
            continue
        if out and (out[-1] > 0) == high:
            out[-1] += us if high else -us
        else:
            out.append(us if high else -us)
    return out


def bits_of(value, count):
    return [(value >> (count - 1 - i)) & 1 for i in range(count)]


def pwm(value, count, short=400, long=1200, repeats=4):
    """Pulse-width: a 1 is a long high then a short low, a 0 a short high then a long low."""
    halves = []
    for _ in range(repeats):
        for b in bits_of(value, count):
            halves += [(True, long if b else short), (False, short if b else long)]
        halves.append((False, GAP_US))
    return runs(halves)


def ppm(value, count, pulse=400, short=400, long=1200, repeats=4, lead=8000):
    """Pulse-distance: every bit a short high, then a short low for 0 and a long low for 1;
    a closing high ends the last gap. A silence comes first."""
    halves = [(False, lead)]
    for _ in range(repeats):
        for b in bits_of(value, count):
            halves += [(True, pulse), (False, long if b else short)]
        halves += [(True, pulse), (False, GAP_US)]
    return runs(halves)


def manchester(value, count, t=500, repeats=3):
    """Manchester, T = 500 us: a 1 is high then low, a 0 low then high."""
    halves = []
    for _ in range(repeats):
        for b in bits_of(value, count):
            halves += [(True, t), (False, t)] if b else [(False, t), (True, t)]
        halves.append((False, GAP_US))
    return runs(halves)


def dense():
    """2400 symbols of 100 us: more than a capture can hold."""
    return [100 if i % 2 == 0 else -100 for i in range(2400)]


def slow():
    """Ten symbols of 300 ms: 3 s, more than a capture spans."""
    return [300_000 if i % 2 == 0 else -300_000 for i in range(10)]


def scenes():
    """(name, description, symbols) — the order is the emu_sg_scene numbering, 1-based."""
    return [
        ("pwm24", "pulse-width, 24 bits 0xA5C3E1, 400/1200 us, 4 repeats",
         pwm(0xA5C3E1, 24)),
        ("ppm24", "pulse-distance, 24 bits 0x5A3CC3, 400 us pulses, 400/1200 us gaps, 4 repeats, "
         "8 ms of silence first", ppm(0x5A3CC3, 24)),
        ("manchester32", "Manchester, 32 bits 0xC3A5F00F, T = 500 us, 3 repeats",
         manchester(0xC3A5F00F, 32)),
        ("dense", "2400 symbols of 100 us: a capture keeps 2048", dense()),
        ("slow", "ten symbols of 300 ms: a capture keeps 2 s", slow()),
    ]


# ---- the header ---------------------------------------------------------------------

def c_array(name, symbols):
    lines = ["static const subghz_sym_t %s[%d] = {" % (name, len(symbols))]
    for i in range(0, len(symbols), 12):
        lines.append("    " + ", ".join(str(v) for v in symbols[i:i + 12]) + ",")
    lines.append("};")
    return lines


def render():
    sc = scenes()
    lines = [
        "/* Generated by scripts/gen_subghz_vectors.py — do not edit.",
        " * SPEC-DERIVED, NOT A RECORDING OF A REAL DEVICE (ADR-0022 §4): generic modulations",
        " * with round-number timings, and the expectations a capture of each must meet,",
        " * computed independently of the firmware. Test infrastructure only. */",
        "/* clang-format off */",
        "#pragma once",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        '#include "stark_subghz_transport.h"',
        "",
        "typedef struct {",
        "    const char *name;",
        "    const subghz_sym_t *sym;",
        "    uint16_t n;          /* symbols the scene plays */",
        "    uint16_t kept_n;     /* symbols a capture keeps of it */",
        "    uint32_t kept_hash;  /* FNV-1a of the kept symbols, little-endian int32 */",
        "    uint32_t kept_us;    /* their total duration */",
        "    uint8_t bins;        /* histogram of the kept symbols */",
        "    uint16_t bin_count[8];",
        "    uint32_t bin_mean[8];",
        "} fake_subghz_scene_t;",
        "",
        "#define FAKE_SUBGHZ_SCENE_COUNT %d" % len(sc),
        "",
    ]
    for i, (name, desc, _) in enumerate(sc):
        lines.append("#define FAKE_SUBGHZ_SCENE_%s %d /* %s */" % (name.upper(), i + 1, desc))
    lines.append("")
    for name, _, syms in sc:
        lines += c_array("k_scene_%s" % name, syms)
    lines += ["", "static const fake_subghz_scene_t k_fake_scenes[FAKE_SUBGHZ_SCENE_COUNT] = {"]
    for name, _, syms in sc:
        kept = capture(syms)
        hist = histogram(kept)
        counts = [c for c, _ in hist] + [0] * (BINS - len(hist))
        means = [m for _, m in hist] + [0] * (BINS - len(hist))
        lines.append('    {"%s", k_scene_%s, %d, %d, 0x%08Xu, %du, %d,' % (
            name, name, len(syms), len(kept), symbol_hash(kept), sum(abs(v) for v in kept),
            len(hist)))
        lines.append("     {%s}," % ", ".join(str(c) for c in counts))
        lines.append("     {%s}}," % ", ".join(str(m) for m in means))
    lines += ["};", ""]
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args(argv)
    text = render()
    if args.check:
        if not OUT.exists() or OUT.read_text() != text:
            print("STALE: %s — run scripts/gen_subghz_vectors.py" % OUT, file=sys.stderr)
            return 1
        return 0
    OUT.write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
