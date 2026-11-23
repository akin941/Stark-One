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

STARK-0604 adds the decoder vectors (test/fake/fake_subghz_decode_vectors.h, host tests only):
captures built from known bits (TASKS.md STARK-0604 definitions of pulse-width, pulse-distance
and Manchester, with +-10 % jitter, tolerance edges, noise and the limit cases) and an
INDEPENDENT reference decoder (ref_decode, written here from docs/subghz-decode.md and not from
components/stark_subghz/subghz_decode.c). Every vector's expectation is built from the bits it
was made of, and generation fails unless the reference decoder agrees; the firmware's results
(including the measured short and long clusters, to the microsecond) are then checked against
the reference's.

Usage:
  gen_subghz_vectors.py            write the generated headers under test/fake/
  gen_subghz_vectors.py --check    exit 1 if a committed file differs (CI lint)
"""
import argparse
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "test/fake/fake_subghz_scenes.h"
OUT_DEC = ROOT / "test/fake/fake_subghz_decode_vectors.h"

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


# ---- the reference decoder (TASKS.md STARK-0604, docs/subghz-decode.md) ---------------------
#
# Written from the rules in docs/subghz-decode.md, with lists where the firmware streams, so
# that an error in one is unlikely to be repeated in the other. Integer arithmetic only: a mean
# is the nearest integer (halves up), "within 25 %" of m is 4 * |d - m| <= m.

DEC_MIN_BITS = 8
DEC_MAX_BITS = 128
DEC_MAX_BURSTS = 8
GAP_FACTOR = 4


def mean_round(vals):
    return (2 * sum(vals) + len(vals)) // (2 * len(vals))


def near(d, m):
    return 4 * abs(d - m) <= m


def two_means(vals):
    """(short mean, long mean, threshold) or None unless two clusters, long >= 1.5 x short."""
    lo, hi = min(vals), max(vals)
    if len(vals) < 2 or lo == hi:
        return None
    t = (lo + hi) // 2
    for p in range(5):
        sm = mean_round([v for v in vals if v <= t])
        lm = mean_round([v for v in vals if v > t])
        nxt = (sm + lm) // 2
        if nxt == t or p == 4:
            break
        t = nxt
    if 2 * lm < 3 * sm:
        return None
    return sm, lm, t


def bits_hex(bits):
    """Two lower-case hex digits per started byte, first bit most significant, zero padded."""
    out = ""
    for i in range(0, len(bits), 8):
        chunk = bits[i:i + 8]
        v = 0
        for b in chunk:
            v = v * 2 + b
        out += "%02x" % (v << (8 - len(chunk)))
    return out


def ref_ppm(d):
    """d: durations of H L H ... H. Every high equal, lows short or long: 0 or 1."""
    highs, lows = d[0::2], d[1::2]
    if not DEC_MIN_BITS <= len(lows) <= DEC_MAX_BITS:
        return None
    pulse = mean_round(highs)
    if not all(near(h, pulse) for h in highs):
        return None
    c = two_means(lows)
    if c is None:
        return None
    s, l, t = c
    bits = []
    for x in lows:
        one = x > t
        if not near(x, l if one else s):
            return None
        bits.append(1 if one else 0)
    return ("ppm", bits, s, l)


def ref_pwm(d):
    """d: durations of H L H ... H. Highs short or long (long = 1); every low what the
    period leaves of its high."""
    highs, lows = d[0::2], d[1::2]
    if not DEC_MIN_BITS <= len(highs) <= DEC_MAX_BITS:
        return None
    c = two_means(highs)
    if c is None:
        return None
    s, l, t = c
    period = mean_round([h + lw for h, lw in zip(highs, lows)])
    bits = []
    for i, h in enumerate(highs):
        one = h > t
        cl = l if one else s
        if not near(h, cl):
            return None
        if i < len(lows) and (period <= cl or not near(lows[i], period - cl)):
            return None
        bits.append(1 if one else 0)
    return ("pwm", bits, s, l)


def ref_manchester(d):
    """d: durations of H L H ... H, every one T or 2T. High-then-low is a 1."""
    c = two_means(d)
    if c is None or 2 * c[1] > 5 * c[0]:
        return None
    s, l, t = c
    if not all(near(x, l if x > t else s) for x in d):
        return None
    halves = []
    for i, x in enumerate(d):
        halves += [1 if i % 2 == 0 else 0] * (2 if x > t else 1)
    for lead in ([], [0]):                 # the burst starts on a cell or on its second half
        seq = lead + halves
        if len(seq) % 2:
            seq = seq + [0]                # the last high's low half is the gap
        pairs = [(seq[i], seq[i + 1]) for i in range(0, len(seq), 2)]
        if all(a != b for a, b in pairs) and DEC_MIN_BITS <= len(pairs) <= DEC_MAX_BITS:
            return ("manchester", [a for a, _ in pairs], s, l)
    return None


def ref_kinds(d):
    """Every kind that reads the burst d on its own (the decoder refuses an ambiguous one)."""
    return [r for r in (ref_ppm(d), ref_pwm(d), ref_manchester(d)) if r is not None]


def ref_decode(symbols):
    """The reference result for a capture: {decoded, unknown, omitted, bursts: [dict]}."""
    for i, v in enumerate(symbols):
        if v == 0 or abs(v) > MAX_SYMBOL_US or (v > 0) != (i % 2 == 0):
            raise ValueError("not a capture")
    if len(symbols) > MAX_SYMBOLS:
        raise ValueError("too long")
    res = {"decoded": 0, "unknown": 0, "omitted": 0, "bursts": []}
    if not symbols:
        return res
    shortest = next((m for c, m in histogram(symbols) if c >= 2), 0)
    if shortest == 0:
        return res
    gap = GAP_FACTOR * shortest
    bursts, cur = [], []
    for v in symbols:
        if v < 0 and -v > gap:
            bursts.append(cur)
            cur = []
        else:
            cur.append(abs(v))
    if cur:
        bursts.append(cur)
    for d in bursts:
        if len(d) % 2 == 0:
            d = d[:-1]                      # the low after the last high is not examined
        found = ref_kinds(d)
        if len(found) != 1:
            res["unknown"] += 1
            continue
        kind, bits, s, l = found[0]
        res["decoded"] += 1
        hexs = bits_hex(bits)
        for b in res["bursts"]:
            if (b["kind"], b["bits"], b["hex"]) == (kind, len(bits), hexs):
                b["repeats"] += 1
                break
        else:
            if len(res["bursts"]) == DEC_MAX_BURSTS:
                res["omitted"] += 1
            else:
                res["bursts"].append({"kind": kind, "bits": len(bits), "hex": hexs,
                                      "repeats": 1, "short": s, "long": l})
    return res


# ---- the decoder vectors -----------------------------------------------------------------

class Rng:
    """xorshift32: the same stream on every Python."""

    def __init__(self, seed):
        self.x = seed & 0xFFFFFFFF or 1

    def next(self):
        x = self.x
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.x = x
        return ((x * 0x9E3779B1) & 0xFFFFFFFF) >> 7  # mixed: the top bits are the good ones

    def below(self, n):
        return self.next() % n

    def between(self, a, b):
        return a + self.below(b - a + 1)

    def bits(self, count):
        v = 0
        for _ in range(count):
            v = v * 2 + (self.next() & 1)
        return v


def jitter(symbols, rng, pct=10):
    """Every symbol scaled by a random factor within +-pct %, its sign kept."""
    out = []
    for v in symbols:
        f = 1000 + rng.between(-pct * 10, pct * 10)
        out.append((1 if v > 0 else -1) * max(1, (abs(v) * f + 500) // 1000))
    return out


def h_pwm(value, count, short, long):
    halves = []
    for b in bits_of(value, count):
        halves += [(True, long if b else short), (False, short if b else long)]
    return halves


def h_ppm(value, count, pulse, short, long):
    halves = []
    for b in bits_of(value, count):
        halves += [(True, pulse), (False, long if b else short)]
    return halves + [(True, pulse)]       # the closing pulse ends the last gap


def h_man(value, count, t):
    halves = []
    for b in bits_of(value, count):
        halves += [(True, t), (False, t)] if b else [(False, t), (True, t)]
    return halves


def frames(halves, repeats, gap=GAP_US, last_gap=True, lead=0):
    """repeats copies of a burst with gaps between; what a capture keeps of it."""
    seq = [(False, lead)] if lead else []
    for r in range(repeats):
        seq += halves
        if r + 1 < repeats or last_gap:
            seq.append((False, gap))
    return capture(runs(seq))


def spec_bits(count, rng, varied=True):
    while True:
        v = rng.bits(count)
        if not varied or 0 < v < (1 << count) - 1:
            return v


def expect_one(kind, value, count, repeats, ref):
    """The expectation of a vector made of one frame repeated: from the bits it was made of;
    the clusters the reference measured."""
    b = ref["bursts"][0] if ref["bursts"] else None
    return {
        "decoded": repeats, "unknown": 0, "omitted": 0,
        "bursts": [{"kind": kind, "bits": count, "hex": bits_hex(bits_of(value, count)),
                    "repeats": repeats, "short": b["short"] if b else 0,
                    "long": b["long"] if b else 0}],
    }


def kind_vector(kind, rng, count, repeats, jit=10, short=None, ratio=None, last_gap=True,
                name=None):
    """A capture of one frame of `count` bits in `kind`, repeated, jittered by +-jit %."""
    short = short or rng.between(20, 90) * 10
    ratio = ratio or (25, 30)[rng.below(2)]
    long = short * ratio // 10
    value = spec_bits(count, rng)
    if kind == "pwm":
        halves = h_pwm(value, count, short, long)
        lead = 0
    elif kind == "ppm":
        pulse = short * (100, 125)[rng.below(2)] // 100
        halves = h_ppm(value, count, pulse, short, long)
        lead = rng.between(0, 1) * 8000
    else:
        halves = h_man(value, count, short)
        lead = 0
    sym = frames(halves, repeats, last_gap=last_gap, lead=lead)
    if jit:
        sym = jitter(sym, rng, jit)
    ref = ref_decode(sym)
    want = expect_one(kind, value, count, repeats, ref)
    return {"name": name or "%s%d_%d_%d" % (kind, count, short, rng.next() % 100),
            "sym": sym, "jit": jit, "expect": want, "ref": ref}


EDGE_FACTORS = {
    # (kind, target): [(above in, above out, below in, below out)] in permille of the symbol:
    # 15 permille either side of where the reference first refuses the burst. The outlier
    # moves its own cluster's mean (a cluster has 12 to 25 members), so the edge is not
    # exactly 25 %; the reference found it
    ("pwm", "short_high"): [(1265, 1295, 748, 718)],
    ("pwm", "long_high"): [(1265, 1295, 747, 717)],
    ("pwm", "long_low"): [(1250, 1280, 757, 727)],
    ("ppm", "pulse"): [(1250, 1280, 756, 726)],
    ("ppm", "long_low"): [(1265, 1295, 747, 717)],
    ("manchester", "T"): [(1242, 1272, 759, 729)],
    ("manchester", "2T"): [(1272, 1302, 765, 735)],
}

EDGE_TARGETS = {
    # kind: (frame, {target: predicate on a symbol})
    "pwm": {"short_high": lambda v: v > 0 and v == 400,
            "long_high": lambda v: v > 0 and v == 1200,
            "long_low": lambda v: v < 0 and v == -1200},
    "ppm": {"pulse": lambda v: v > 0 and v == 400,
            "long_low": lambda v: v < 0 and v == -1200},
    "manchester": {"T": lambda v: abs(v) == 500,
                   "2T": lambda v: abs(v) == 1000},
}


def edge_vector(kind, target, factor_pm, accept):
    """One symbol of the first burst scaled to factor_pm/1000 of itself: within 25 % of its
    cluster (every burst still decodes) or beyond it (that burst is unknown, the repeats
    decode)."""
    repeats = 3
    if kind == "pwm":
        value, count = 0xA5C3E1, 24
        halves = h_pwm(value, count, 400, 1200)
    elif kind == "ppm":
        value, count = 0xA5C3E1, 24
        halves = h_ppm(value, count, 400, 400, 1200)
    else:
        value, count = 0xC3A5F00F, 32
        halves = h_man(value, count, 500)
    sym = frames(halves, repeats)
    pick = EDGE_TARGETS[kind][target]
    i = next(i for i, v in enumerate(sym) if pick(v))
    sym[i] = (1 if sym[i] > 0 else -1) * ((abs(sym[i]) * factor_pm + 500) // 1000)
    ref = ref_decode(sym)
    want = expect_one(kind, value, count, repeats - (0 if accept else 1), ref)
    if not accept:
        want["decoded"], want["unknown"] = repeats - 1, 1
    return {"name": "edge_%s_%s_%d_%s" % (kind, target, factor_pm, "in" if accept else "out"),
            "sym": sym, "jit": 0, "expect": want, "ref": ref}


def plain_vector(name, sym, expect):
    return {"name": name, "sym": sym, "jit": 0, "expect": expect, "ref": ref_decode(sym)}


def none_expected(unknown=0):
    return {"decoded": 0, "unknown": unknown, "omitted": 0, "bursts": []}


def decode_vectors():
    """Every vector: {name, sym, jit, expect, ref}. Generation fails unless the reference
    decoder agrees with each vector's expectation (the bits it was built from)."""
    vecs = []
    rng = Rng(0x5EED0604)

    # 24 captures per kind, +-10 % jitter, 8 to 128 bits
    sizes = [8, 9, 12, 16, 16, 20, 24, 24, 24, 28, 32, 32, 36, 40, 48, 48, 56, 64, 80, 96,
             104, 112, 120, 128]
    for kind in ("pwm", "ppm", "manchester"):
        for i, count in enumerate(sizes):
            repeats = 1 if count >= 80 else 2 + i % 2
            vecs.append(kind_vector(kind, rng, count, repeats, 10, last_gap=(i % 4 != 3)))

    # the same, exact (no jitter) at round numbers
    for kind in ("pwm", "ppm", "manchester"):
        vecs.append(kind_vector(kind, rng, 24, 3, 0, short=400, ratio=30 if kind != "manchester"
                                else 10, name="exact_%s24" % kind))

    # tolerance edges: just inside and just outside 25 % of the cluster, above and below
    for kind, targets in EDGE_TARGETS.items():
        for target in targets:
            for up_in, up_out, down_in, down_out in EDGE_FACTORS[(kind, target)]:
                vecs.append(edge_vector(kind, target, up_in, True))
                vecs.append(edge_vector(kind, target, up_out, False))
                vecs.append(edge_vector(kind, target, down_in, True))
                vecs.append(edge_vector(kind, target, down_out, False))

    # bit-count limits: 7 / 8 and 128 / 129, one burst, no closing gap
    for kind in ("pwm", "ppm", "manchester"):
        for count, ok in ((7, False), (8, True), (128, True), (129, False)):
            r = Rng(0xC0DE + count)
            v = kind_vector(kind, r, count, 1, 0, short=400, ratio=30 if kind != "manchester"
                            else 10, last_gap=False, name="limit_%s_%d" % (kind, count))
            if not ok:
                v["expect"] = none_expected(1)
            vecs.append(v)
    # Manchester of 129 bits that ends on a 0 cell, and on a 1 cell: both over the limit
    for last in (0, 1):
        r = Rng(0xBEEF + last)
        value = (spec_bits(128, r) << 1) | last
        sym = frames(h_man(value, 129, 400), 1, last_gap=True)
        vecs.append(plain_vector("limit_manchester_129_end%d" % last, sym, none_expected(1)))
    # and a Manchester that starts on a 0 cell and one that starts on a 1 cell, 0 to 1 ends
    for first in (0, 1):
        for last in (0, 1):
            r = Rng(0xFACE + first * 2 + last)
            value = (first << 31) | (spec_bits(30, r) << 1) | last
            sym = frames(h_man(value, 32, 600), 2, last_gap=True)
            want = expect_one("manchester", value, 32, 2, ref_decode(sym))
            vecs.append(plain_vector("man_first%d_last%d" % (first, last), sym, want))

    # not a decode
    rng = Rng(0x0DD5)
    for i in range(16):
        n = rng.between(30, 150)
        sym = [(rng.between(50, 5000)) * (1 if k % 2 == 0 else -1) for k in range(n)]
        vecs.append(plain_vector("noise_%02d" % i, sym, None))
    vecs.append(plain_vector("single_pulse", [500, -500], none_expected(1)))
    vecs.append(plain_vector("lonely_high", [500], none_expected(0)))
    vecs.append(plain_vector("square_wave", [500 if k % 2 == 0 else -500 for k in range(40)],
                             none_expected(1)))
    halves = h_ppm(0, 24, 400, 400, 1200)
    vecs.append(plain_vector("ppm_all_short", frames(halves, 2), none_expected(2)))
    halves = h_pwm(0xFFFFFF, 24, 400, 1200)
    vecs.append(plain_vector("pwm_all_long", frames(halves, 2), none_expected(2)))
    halves = h_man(0xFFFFFF, 24, 500)
    vecs.append(plain_vector("man_no_2t", frames(halves, 2), none_expected(2)))
    # two kinds of timing in one burst
    halves = h_pwm(0xA5C, 12, 400, 1200) + h_man(0x5A3, 12, 500)
    vecs.append(plain_vector("mixed_in_a_burst", frames(halves, 2), none_expected(2)))
    # two bursts of different kinds in one capture: each decodes
    sym = capture(runs([(False, 8000)] + h_ppm(0x5A3CC3, 24, 400, 400, 1200) +
                       [(False, GAP_US)] + h_pwm(0xA5C3E1, 24, 400, 1200) + [(False, GAP_US)]))
    want = {"decoded": 2, "unknown": 0, "omitted": 0, "bursts": [
        {"kind": "ppm", "bits": 24, "hex": "5a3cc3", "repeats": 1, "short": 400, "long": 1200},
        {"kind": "pwm", "bits": 24, "hex": "a5c3e1", "repeats": 1, "short": 400, "long": 1200}]}
    vecs.append(plain_vector("two_kinds_two_bursts", sym, want))
    # more distinct bursts than a result keeps: eight stored, two omitted
    seq = []
    for k in range(10):
        seq += h_pwm(0x100 + k * 37, 16, 400, 1200) + [(False, GAP_US)]
    sym = capture(runs(seq))
    vecs.append(plain_vector("ten_distinct_pwm", sym, None))
    # a burst two kinds both read is not guessed: pulse-distance with T and 2T gaps and a
    # single 1 (high-low-low-high ... also reads as Manchester)
    sym = frames(h_ppm(0x020, 12, 500, 500, 1000), 2)
    if len(ref_kinds([abs(v) for v in sym[:25]])) != 2:
        raise SystemExit("the ambiguous vector is not ambiguous")
    vecs.append(plain_vector("ambiguous_ppm_manchester", sym, none_expected(2)))

    # differential: valid captures with a few symbols scaled by 0.5 .. 2; the expectation is
    # the reference's (up to three distinct bursts, eight for the one limit vector)
    rng = Rng(0xD1FF)
    made = 0
    while made < 48:
        kind = ("pwm", "ppm", "manchester")[made % 3]
        base = kind_vector(kind, rng, rng.between(12, 24), 3, 0, name="m")
        sym = list(base["sym"])
        for _ in range(rng.between(1, 3)):
            i = rng.below(len(sym))
            sym[i] = (1 if sym[i] > 0 else -1) * max(1, abs(sym[i]) * rng.between(500, 2000) // 1000)
        ref = ref_decode(sym)
        if len(ref["bursts"]) > 3:
            continue
        vecs.append({"name": "mutated_%02d_%s" % (made, kind), "sym": sym, "jit": 0,
                     "expect": ref, "ref": ref})
        made += 1

    for v in vecs:
        if v["expect"] is None:                       # noise and the like: nothing decodes
            v["expect"] = dict(v["ref"])
            if v["name"].startswith("noise") and v["ref"]["decoded"] != 0:
                raise SystemExit("%s: reference decoded noise" % v["name"])
        ref = v["ref"]
        want = v["expect"]
        keys = ("decoded", "unknown", "omitted")
        if any(ref[k] != want[k] for k in keys) or [
                (b["kind"], b["bits"], b["hex"], b["repeats"]) for b in ref["bursts"]] != [
                (b["kind"], b["bits"], b["hex"], b["repeats"]) for b in want["bursts"]]:
            raise SystemExit("%s: reference %r != built %r" % (v["name"], ref, want))
        v["expect"] = ref                             # with the measured clusters
    return vecs


def scene_expectations():
    """The reference's reading of each scene as a capture keeps it (STARK-0604 AC 1)."""
    return [ref_decode(capture(syms)) for _, _, syms in scenes()]


def render_decode():
    vecs = decode_vectors()
    lines = [
        "/* Generated by scripts/gen_subghz_vectors.py — do not edit.",
        " * SPEC-DERIVED, NOT A RECORDING OF A REAL DEVICE (ADR-0022 §4): captures built from",
        " * known bits with generic pulse-width, pulse-distance and Manchester timing (+-10 %",
        " * jitter, tolerance edges, noise, limits) and what a decoder must read from each, as",
        " * built and as the independent reference decoder reads it. Host tests only. */",
        "/* clang-format off */",
        "#pragma once",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        '#include "stark_subghz_transport.h"',
        "",
        "typedef struct {",
        "    const char *kind;   /* \"pwm\" / \"ppm\" / \"manchester\" */",
        "    uint8_t bits;",
        "    const char *hex;",
        "    uint16_t repeats;",
        "    uint32_t short_us;  /* the measured clusters */",
        "    uint32_t long_us;",
        "} fake_dec_burst_t;",
        "",
        "typedef struct {",
        "    const char *name;",
        "    const subghz_sym_t *sym;",
        "    uint16_t n;",
        "    uint8_t jitter_pct; /* the jitter the vector was made with */",
        "    uint16_t decoded;   /* bursts that decode (repeats included) */",
        "    uint16_t unknown;   /* bursts that do not */",
        "    uint16_t omitted;   /* decoded, but not among the eight kept */",
        "    uint8_t n_burst;    /* distinct decodes, first seen first */",
        "    fake_dec_burst_t burst[8];",
        "} fake_dec_vector_t;",
        "",
        "#define FAKE_DEC_VECTOR_COUNT %d" % len(vecs),
        "",
    ]
    for i, v in enumerate(vecs):
        lines += c_array("k_dv_%d" % i, v["sym"])

    def entry(name, sym_ref, n, jit, e):
        bursts = ", ".join('{"%s", %d, "%s", %d, %d, %d}' % (
            b["kind"], b["bits"], b["hex"], b["repeats"], b["short"], b["long"])
            for b in e["bursts"])
        return '    {"%s", %s, %d, %d, %d, %d, %d, %d, {%s}},' % (
            name, sym_ref, n, jit, e["decoded"], e["unknown"], e["omitted"], len(e["bursts"]),
            bursts)

    lines += ["", "static const fake_dec_vector_t k_fake_dec_vectors[FAKE_DEC_VECTOR_COUNT] = {"]
    for i, v in enumerate(vecs):
        lines.append(entry(v["name"], "k_dv_%d" % i, len(v["sym"]), v["jit"], v["expect"]))
    lines += ["};", "",
              "/* what each scene of fake_subghz_scenes.h decodes to once captured, by scene "
              "number - 1 */",
              "static const fake_dec_vector_t k_fake_dec_scenes[%d] = {" % len(scenes())]
    for (name, _, _), e in zip(scenes(), scene_expectations()):
        lines.append(entry(name, "NULL", 0, 0, e))
    lines += ["};", ""]
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args(argv)
    outputs = [(OUT, render()), (OUT_DEC, render_decode())]
    if args.check:
        stale = [p for p, text in outputs if not p.exists() or p.read_text() != text]
        for p in stale:
            print("STALE: %s — run scripts/gen_subghz_vectors.py" % p, file=sys.stderr)
        return 1 if stale else 0
    for p, text in outputs:
        p.write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
