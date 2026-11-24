#!/usr/bin/env python3
"""
gen_lab_vectors.py — the frequency and duty edge sets of the lab's measurement (TASKS.md
STARK-0703, ADR-0023 §4) written as test/fake/fake_lab_vectors.h, and the I2C scanner's sweeps,
fingerprints, refusals and hex text (STARK-0704, ADR-0023 §5) written as
test/fake/fake_lab_i2c_vectors.h.

Every vector is SPEC-DERIVED, NOT A RECORDING OF A REAL SIGNAL: a square wave of a round
frequency and duty sampled as the timestamps (CPU cycle counts at 240 MHz) of its edges, with
seeded jitter, glitches, a steady line, a ring overflow, a counter wrap and a few hostile
inputs. The expectation next to each set is computed here by ref_measure(), code written
independently of the firmware's (components/stark_lab/lab_meas.c): exact rational arithmetic
over the definitions in components/stark_lab/lab_meas.h, with the 32-bit counter unwrapped
into plain integers and the edges handled as (time, level) pairs, where the firmware works
on running sums. The firmware is tested against it and never against itself.

Two further checks run at generation time, so a stale or wrong vector cannot be written:
every OK vector's reference result must lie within the accuracy ADR-0023 §4 allows of the
frequency and duty it was made at (frequency +-0.5 % to 10 kHz and +-2 % to 50 kHz, duty +-2
points to 10 kHz and +-5 to 50 kHz), and the capture window is sized by ref_plan(), the
independent restatement of the pre-gate rule, from the signal's own gate count.

The I2C vectors are as independent: ref_sweep() restates the sweep rule of lab_i2c.h (112
probes, three errors in a row end it, a NACK or an ACK starts the count again), ref_fnv1a() is
the textbook 32-bit FNV-1a over the byte string the fingerprint is defined on, ref_refusal() is
the refusal table of ADR-0023 §5 (a module ID EEPROM takes a one-byte pointer and a read only,
the PN532 takes nothing while NFC is confirmed), and ref_hex() the lower-case hex text with the
whole-bytes-that-fit truncation.

Usage:
  gen_lab_vectors.py            write test/fake/fake_lab_vectors.h and fake_lab_i2c_vectors.h
  gen_lab_vectors.py --check    exit 1 if a committed file differs (CI lint)
"""
import argparse
import sys
from fractions import Fraction
from math import floor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "test/fake/fake_lab_vectors.h"
OUT_I2C = ROOT / "test/fake/fake_lab_i2c_vectors.h"

TICK_HZ = 240_000_000
RING = 1024
GATE_MS = 20
WINDOW_MS_MIN = 10
WINDOW_MS_MAX = 4000
TARGET_EDGES = 768
HZ_MAX_X100 = 5_005_000  # 50 kHz + 0.1 %
HZ_MIN_X100 = 99  # 1 Hz less one count of 0.01 Hz
GLITCH = Fraction(1, 1_000_000)  # seconds

OK, TOO_FAST, TOO_SLOW, NO_SIGNAL, OVERFLOW, GLITCH_S, INVALID = range(7)
STATUS_NAMES = ["OK", "TOO_FAST", "TOO_SLOW", "NO_SIGNAL", "OVERFLOW", "GLITCH", "INVALID"]


# ---- the reference: lab_meas.h's definitions, restated -----------------------------------

def round_half_up(x):
    return floor(x + Fraction(1, 2))


def ref_measure(ts, first_level, tick_hz, overflowed):
    """(status, hz_x100, duty_x10, period_ticks, steady_high) for one captured window."""
    if tick_hz == 0:
        return (INVALID, 0, 0, 0, False)
    if overflowed:
        return (OVERFLOW, 0, 0, 0, False)
    if len(ts) == 0:
        return (NO_SIGNAL, 0, 0, 0, bool(first_level))
    if len(ts) > RING:
        return (INVALID, 0, 0, 0, False)
    # the counter is 32 bits wide: unwrap it into plain integers
    times = [ts[0]]
    for a, b in zip(ts, ts[1:]):
        step = (b - a) & 0xFFFFFFFF
        if step >= 2 ** 31:
            return (INVALID, 0, 0, 0, False)
        times.append(times[-1] + step)
    for a, b in zip(times, times[1:]):
        if Fraction(b - a, tick_hz) < GLITCH:
            return (GLITCH_S, 0, 0, 0, False)
    level = bool(first_level)
    edges = []
    for t in times:
        level = not level
        edges.append((t, level))
    rises = [t for t, lvl in edges if lvl]
    if len(rises) < 2:
        return (TOO_SLOW, 0, 0, 0, False)
    span = rises[-1] - rises[0]
    hz_x100 = round_half_up(Fraction((len(rises) - 1) * tick_hz * 100, span))
    if hz_x100 > HZ_MAX_X100:
        return (TOO_FAST, 0, 0, 0, False)
    if hz_x100 < HZ_MIN_X100:
        return (TOO_SLOW, 0, 0, 0, False)
    high = 0
    for i in range(len(rises) - 1):
        falls = [t for t, lvl in edges if not lvl and rises[i] < t < rises[i + 1]]
        assert len(falls) == 1
        high += falls[0] - rises[i]
    duty_x10 = round_half_up(Fraction(high * 1000, span))
    period = round_half_up(Fraction(span, len(rises) - 1))
    return (OK, hz_x100, duty_x10, period, False)


def ref_plan(gate_edges):
    """(too_fast, window_ms) for the edges counted in the pre-gate."""
    if gate_edges == 0:
        return (False, WINDOW_MS_MAX)
    est_hz = Fraction(gate_edges, 2) / Fraction(GATE_MS, 1000)  # periods per second
    if est_hz * 100 > HZ_MAX_X100:
        return (True, 0)
    ms = floor(Fraction(TARGET_EDGES * GATE_MS, gate_edges))
    return (False, max(WINDOW_MS_MIN, min(WINDOW_MS_MAX, ms)))


# ---- the signals --------------------------------------------------------------------------

class Rng:
    """xorshift32: the same sequence on every platform and Python version."""

    def __init__(self, seed):
        self.s = (seed * 2654435761 + 12345) & 0xFFFFFFFF or 1

    def next(self):
        x = self.s
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.s = x
        return x

    def between(self, lo, hi):
        return lo + self.next() % (hi - lo + 1)


def square_transitions(hz_x100, duty_pct, start, end):
    """The transitions (tick, level_after) of a square wave in [start, end), and its level at
    `start`. The wave rises at k * period and falls duty % of a period later."""
    period = Fraction(TICK_HZ * 100, hz_x100)
    high = period * duty_pct / 100
    k0 = floor(Fraction(start) / period)
    level0 = (start - k0 * period) < high
    out = []
    k = k0
    while True:
        rise = floor(k * period)
        fall = floor(k * period + high)
        if rise >= end:
            break
        if start <= rise:
            out.append((rise, True))
        if start <= fall < end:
            out.append((fall, False))
        k += 1
    if out:
        level0 = not out[0][1]  # the level before the first edge is the opposite of its own
    return out, bool(level0)


def jittered(transitions, jitter_ticks, rng):
    """Every edge moved by up to +-jitter_ticks, kept strictly in order."""
    out = []
    prev = None
    for t, lvl in transitions:
        t2 = t + (rng.between(-jitter_ticks, jitter_ticks) if jitter_ticks else 0)
        if prev is not None and t2 <= prev:
            t2 = prev + 1
        out.append((t2, lvl))
        prev = t2
    return out


def wire(transitions, base, first_level):
    """The stamps of a window: absolute ticks plus `base`, wrapped to 32 bits."""
    return [(t + base) & 0xFFFFFFFF for t, _ in transitions], first_level


def min_pulse_ticks(hz_x100, duty_pct):
    period = Fraction(TICK_HZ * 100, hz_x100)
    high = period * duty_pct / 100
    return min(high, period - high)


def capture(hz_x100, duty_pct, seed, base=1_000_000, jitter_us=2, glitch_every=0):
    """One measurement the way the service makes it: a pre-gate counts edges, the window is
    sized by the plan from that count, and the window is captured (a ring that would overflow
    keeps the first 1024 stamps and says so). Returns (stamps, first_level, overflowed, gate)."""
    rng = Rng(seed)
    start = rng.between(0, TICK_HZ // 100)  # the window starts at a random phase
    gate_edges, _ = square_transitions(hz_x100, duty_pct, start, start + GATE_MS * TICK_HZ // 1000)
    too_fast, window_ms = ref_plan(len(gate_edges))
    if too_fast:
        return None  # the service stops at the gate and never captures
    end = start + window_ms * TICK_HZ // 1000
    trans, level0 = square_transitions(hz_x100, duty_pct, start, end)
    # jitter is capped at a quarter of the shortest pulse, so it can never reorder edges
    cap = int(min_pulse_ticks(hz_x100, duty_pct) / 4)
    trans = jittered(trans, min(jitter_us * TICK_HZ // 1_000_000, cap), rng)
    if glitch_every:
        out = []
        for i, (t, lvl) in enumerate(trans):
            out.append((t, lvl))
            if i % glitch_every == glitch_every - 1:
                out.append((t + 120, not lvl))  # 0.5 us after an edge: back again
                out.append((t + 240, lvl))
        trans = out
    over = len(trans) > RING
    trans = trans[:RING]
    ts, first = wire(trans, base, level0)
    return ts, first, over, len(gate_edges)


# ---- the catalogue ------------------------------------------------------------------------

def tolerances(hz_x100):
    """(frequency ppm, duty in 0.1 % units) ADR-0023 §4 allows at this frequency."""
    return (5000, 20) if hz_x100 <= 1_000_000 else (20000, 50)


def vectors():
    vs = []
    seed = 1

    def add(name, ts, first, over, nominal=None, tick=TICK_HZ):
        st, hz, duty, period, steady = ref_measure(ts, first, tick, over)
        ts = [x & 0xFFFFFFFF for x in ts]
        v = {"name": name, "ts": list(ts), "first": bool(first), "over": bool(over), "tick": tick,
             "status": st, "hz": hz, "duty": duty, "period": period, "steady": steady,
             "nominal_hz": 0, "nominal_duty": 0, "tol_hz": 0, "tol_duty": 0}
        if nominal is not None:
            assert st == OK, "%s: expected a measurement, the reference says %s" % (
                name, STATUS_NAMES[st])
            hz_n, duty_n = nominal
            tol_hz, tol_duty = tolerances(hz_n)
            err_ppm = abs(hz - hz_n) * 1_000_000 // hz_n
            assert err_ppm <= tol_hz, "%s: %d ppm off" % (name, err_ppm)
            assert abs(duty - duty_n * 10) <= tol_duty, "%s: duty %d vs %d" % (name, duty, duty_n)
            v.update(nominal_hz=hz_n, nominal_duty=duty_n * 10, tol_hz=tol_hz, tol_duty=tol_duty)
        vs.append(v)

    # the catalogue of ADR-0023 §4: every frequency at every duty. A pair whose shortest pulse
    # is about a microsecond or less is a glitch, and the reference says so.
    for hz in (1, 2, 10, 100, 1000, 10000, 25000, 50000):
        for duty in (1, 10, 25, 50, 75, 99):
            seed += 1
            ts, first, over, _ = capture(hz * 100, duty, seed)
            name = "%s_%dpct" % ("%dhz" % hz if hz < 1000 else "%dkhz" % (hz // 1000), duty)
            st = ref_measure(ts, first, TICK_HZ, over)[0]
            if st == OK:
                add(name, ts, first, over, nominal=(hz * 100, duty))
            else:
                assert st == GLITCH_S and min_pulse_ticks(hz * 100, duty) <= 2 * 240, name
                add(name + "_glitch", ts, first, over)
    # jitter at its full +-2 us where the pulses allow it, and the same signals read from
    # the other level (the first edge is then a falling one)
    for hz, duty in ((100, 50), (1000, 25), (10000, 50)):
        seed += 1
        ts, first, over, _ = capture(hz * 100, duty, seed, jitter_us=2)
        add("jitter2us_%dhz_%dpct" % (hz, duty), ts, first, over, nominal=(hz * 100, duty))
    # the limits and just outside them (a gate that already says "too fast" makes no capture)
    for label, hz_x100, duty in (("1p00hz", 100, 50), ("50p0khz", 5_000_000, 50),
                                 ("50p2khz", 5_020_000, 50), ("0p25hz", 25, 50)):
        seed += 1
        got = capture(hz_x100, duty, seed)
        if got is None:
            assert ref_plan(int(hz_x100 * 2 * GATE_MS / 1000 / 100))[0]
            continue
        ts, first, over, _ = got
        st = ref_measure(ts, first, TICK_HZ, over)[0]
        if st == OK:
            add("limit_%s" % label, ts, first, over, nominal=(hz_x100, duty))
        else:
            add("limit_%s" % label, ts, first, over)
    # past 50 kHz the window is cut by the gate in the service; a captured stretch of 5 ms
    # at 80 kHz and 51 kHz is what compute sees when it is called anyway
    for label, hz_x100 in (("80khz", 8_000_000), ("51khz", 5_100_000), ("0p5hz", 50), ("0p98hz", 98)):
        seed += 1
        rng = Rng(seed)
        start = rng.between(0, 1000)
        win = 5 * TICK_HZ // 1000 if hz_x100 >= 1_000_000 else 4000 * TICK_HZ // 1000
        trans, lvl = square_transitions(hz_x100, 50, start, start + win)
        ts, first = wire(trans, 1_000_000, lvl)
        add("range_%s" % label, ts, first, False)
    # a steady line, high and low: no edge at all
    add("steady_high", [], True, False)
    add("steady_low", [], False, False)
    # fewer than two rising edges
    add("one_edge_up", [5000], False, False)
    add("one_edge_down", [5000], True, False)
    add("two_edges_one_rise", [5000, 125000], False, False)
    add("three_edges_falling_first", [5000, 125000, 245000], True, False)
    # a ring that filled up: reported, never a number
    seed += 1
    ts, first, _, _ = capture(500_000, 50, seed)
    full = [(1_000_000 + i * 120_000) & 0xFFFFFFFF for i in range(RING)]
    add("overflow_ring_full", full, False, True)
    add("overflow_whatever_it_holds", ts, first, True)
    # the CPU counter wraps inside the window: 1 kHz at 50 % across 2^32
    for label, base in (("wrap_1khz", (1 << 32) - 4_000_000), ("wrap_edge", (1 << 32) - 1)):
        seed += 1
        rng = Rng(seed)
        trans, lvl = square_transitions(100_000, 50, 0, 384 * TICK_HZ // 1000)
        ts, first = wire(trans, base, lvl)
        add(label, ts, first, False, nominal=(100_000, 50))
    # a glitch train on a 1 kHz signal: sub-microsecond pulses every fourth edge
    seed += 1
    ts, first, over, _ = capture(100_000, 50, seed, glitch_every=4)
    add("glitch_train_1khz", ts, first, over)
    # hostile input: stamps that go backwards, two edges at one instant, a zero clock
    add("backwards", [1000, 241000, 120500, 481000], False, False)
    add("backwards_wrapped", [0x10, 0xFFFFFF00, 0x20], False, False)
    add("duplicate_stamps", [1000, 121000, 121000, 241000], False, False)
    add("all_zero_steps", [7, 7, 7, 7, 7], False, False)
    add("zero_clock", [1000, 121000, 241000], False, False, tick=0)
    add("too_long", [i * 1000 for i in range(RING + 1)], False, False)
    # a step of exactly 2^31 - 1 ticks is a forward one, 2^31 is not
    add("step_max_forward", [0, 0x7FFFFFFF, 0xFFFFFFFE, 0x17FFFFFFD], False, False)
    add("step_backwards_min", [0, 0x80000000, 0x80000001], False, False)
    # the biggest sums: 1024 steps of 2^31 - 1 ticks, and the same on a clock of 2^32 - 1 Hz
    huge = [i * 0x7FFFFFFF for i in range(RING)]
    add("huge_steps_ring", huge, False, False)
    add("huge_steps_fast_clock", huge, False, False, tick=0xFFFFFFFF)
    return vs


def plan_vectors():
    out = []
    for c in (0, 1, 2, 3, 5, 10, 20, 38, 39, 40, 100, 383, 384, 767, 768, 769, 1000, 1500, 2000,
              2001, 2002, 2003, 2004, 3000, 4096):
        too_fast, ms = ref_plan(c)
        out.append((c, too_fast, ms))
    return out


# ---- the header ---------------------------------------------------------------------------

def render():
    vs = vectors()
    lines = [
        "/* Generated by scripts/gen_lab_vectors.py — do not edit.",
        " * SPEC-DERIVED, NOT A RECORDING OF A REAL SIGNAL (ADR-0023 §4): square waves of round",
        " * frequencies and duties as the timestamps of their edges (240 MHz), with seeded jitter,",
        " * glitches, a steady line, an overflow, a counter wrap and hostile input, and the result",
        " * a measurement of each must give, computed independently of the firmware.",
        " * Test infrastructure only. */",
        "/* clang-format off */",
        "#pragma once",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "/* status: 0 OK, 1 TOO_FAST, 2 TOO_SLOW, 3 NO_SIGNAL, 4 OVERFLOW, 5 GLITCH, 6 INVALID */",
        "typedef struct {",
        "    const char *name;",
        "    const uint32_t *ts;",
        "    size_t n;",
        "    uint8_t first_level;",
        "    uint8_t overflowed;",
        "    uint32_t tick_hz;",
        "    uint8_t status;       /* the reference's */",
        "    uint32_t hz_x100;     /* ... exact, rounded to 0.01 Hz */",
        "    uint16_t duty_x10;    /* ... rounded to 0.1 % */",
        "    uint32_t period_ticks;",
        "    uint8_t steady_high;",
        "    uint32_t nominal_hz_x100;  /* what the wave was made at (0: no accuracy check) */",
        "    uint16_t nominal_duty_x10;",
        "    uint32_t tol_hz_ppm;       /* what ADR-0023 §4 allows of it */",
        "    uint16_t tol_duty_x10;",
        "} fake_lab_vec_t;",
        "",
        "typedef struct {",
        "    uint32_t gate_edges;",
        "    uint8_t too_fast;",
        "    uint16_t window_ms;",
        "} fake_lab_plan_vec_t;",
        "",
    ]
    for i, v in enumerate(vs):
        if v["ts"]:
            body = ",".join(str(x) for x in v["ts"])
            # wrap the array text at about 100 columns
            parts, cur = [], ""
            for tok in body.split(","):
                if len(cur) + len(tok) + 1 > 96:
                    parts.append(cur)
                    cur = ""
                cur += tok + ","
            parts.append(cur)
            lines.append("static const uint32_t fake_lab_ts_%d[] = {" % i)
            lines.extend("    " + p for p in parts)
            lines.append("};")
    lines.append("")
    lines.append("static const fake_lab_vec_t fake_lab_vecs[] = {")
    for i, v in enumerate(vs):
        ref = "fake_lab_ts_%d" % i if v["ts"] else "NULL"
        lines.append(
            '    {"%s", %s, %d, %d, %d, %du, %d, %du, %du, %du, %d, %du, %du, %du, %du},' % (
                v["name"], ref, len(v["ts"]), int(v["first"]), int(v["over"]), v["tick"],
                v["status"], v["hz"], v["duty"], v["period"], int(v["steady"]),
                v["nominal_hz"], v["nominal_duty"], v["tol_hz"], v["tol_duty"]))
    lines.append("};")
    lines.append("#define FAKE_LAB_VECS (sizeof(fake_lab_vecs) / sizeof(fake_lab_vecs[0]))")
    lines.append("")
    lines.append("static const fake_lab_plan_vec_t fake_lab_plan_vecs[] = {")
    for c, too_fast, ms in plan_vectors():
        lines.append("    {%du, %d, %du}," % (c, int(too_fast), ms))
    lines.append("};")
    lines.append(
        "#define FAKE_LAB_PLAN_VECS (sizeof(fake_lab_plan_vecs) / sizeof(fake_lab_plan_vecs[0]))")
    lines.append("")
    return "\n".join(lines)


# ---- the I2C scanner (STARK-0704): the reference and its vectors --------------------------

I2C_MIN, I2C_MAX = 0x08, 0x77
I2C_RUN = 3
I2C_PN532 = 0x24
I2C_EEPROMS = (0x50, 0x51)
SWEEP_OK, SWEEP_IO = 0, 1
REFUSE_NONE, REFUSE_EEPROM, REFUSE_NFC = 0, 1, 2


def ref_sweep(acks, errs):
    """(found, errors, status, probes): `acks` answer, `errs` time out, the rest NACK."""
    found, errors, run, probes = [], 0, 0, 0
    status = SWEEP_OK
    for a in range(I2C_MIN, I2C_MAX + 1):
        probes += 1
        if a in errs:
            errors += 1
            run += 1
            if run >= I2C_RUN:
                status = SWEEP_IO
                break
        else:
            run = 0
            if a in acks:
                found.append(a)
    return found, errors, status, probes


def ref_fnv1a(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def ref_fp_write(addr, reg, reg16, data):
    return ref_fnv1a(b"W" + bytes([addr, 1 if reg16 else 0, reg >> 8, reg & 0xFF, len(data)])
                     + bytes(data))


def ref_fp_dump(addr):
    return ref_fnv1a(b"D" + bytes([addr]))


def ref_refusal(addr, wn, rn, nfc):
    if addr in I2C_EEPROMS and (rn == 0 or wn > 1):
        return REFUSE_EEPROM
    if addr == I2C_PN532 and nfc:
        return REFUSE_NFC
    return REFUSE_NONE


def ref_hex(data, sz, spaced):
    """The text that fits a buffer of `sz` bytes (NUL included): whole bytes only."""
    if sz == 0:
        return ""
    out = ""
    for i, b in enumerate(data):
        piece = ("%02x" % b) if (i == 0 or not spaced) else (" %02x" % b)
        if len(out) + len(piece) + 1 > sz:
            break
        out += piece
    return out


def sweep_vectors():
    allc = set(range(I2C_MIN, I2C_MAX + 1))
    return [
        ("empty", set(), set()),
        ("one", {0x50}, set()),
        ("edges", {0x08, 0x77}, set()),
        ("all_112", allc, set()),
        ("five", {0x23, 0x3C, 0x48, 0x68, 0x76}, set()),
        ("one_timeout", {0x23, 0x68}, {0x48}),
        ("two_in_a_row", {0x50}, {0x30, 0x31}),
        ("three_at_the_start", {0x23}, {0x08, 0x09, 0x0A}),
        ("three_after_a_find", {0x23, 0x50}, {0x40, 0x41, 0x42}),
        ("a_nack_between", set(), {0x30, 0x31, 0x33, 0x34}),
        ("an_ack_between", {0x32}, {0x30, 0x31, 0x33, 0x34}),
        ("two_at_the_end", {0x70}, {0x76, 0x77}),
        ("three_at_the_end", {0x70}, {0x75, 0x76, 0x77}),
        ("scattered", {0x15, 0x25}, {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70}),
        ("all_time_out", set(), allc),
    ]


def refusal_vectors():
    out = []
    for addr in (0x08, 0x23, 0x24, 0x50, 0x51, 0x52, 0x77):
        for wn, rn in ((1, 1), (2, 1), (1, 16), (3, 1), (1, 0), (2, 0), (10, 0), (0, 16)):
            for nfc in (False, True):
                out.append((addr, wn, rn, nfc, ref_refusal(addr, wn, rn, nfc)))
    return out


def fp_vectors():
    out = []
    for addr, reg, reg16, data in (
            (0x68, 0x6B, False, [0x01]),
            (0x68, 0x6B, False, [0x00]),
            (0x68, 0x6C, False, [0x01]),
            (0x69, 0x6B, False, [0x01]),
            (0x68, 0x6B, True, [0x01]),
            (0x68, 0x006B, True, [0x02]),
            (0x68, 0x016B, True, [0x01]),
            (0x48, 0x01, False, [0x60, 0xA0]),
            (0x76, 0xF4, False, [0x27]),
            (0x50, 0x00, False, [0xFF] * 8),
            (0x77, 0xFF, False, list(range(8))),
            (0x08, 0x00, False, [0x00])):
        out.append(("W", addr, reg, reg16, data, ref_fp_write(addr, reg, reg16, data)))
    for addr in (0x08, 0x50, 0x68, 0x77):
        out.append(("D", addr, 0, False, [], ref_fp_dump(addr)))
    return out


def hex_vectors():
    out = []
    ramp = list(range(0x00, 0x100, 0x11))[:16]
    for data in ([], [0x00], [0x00, 0xFF], [0xA5, 0x5A, 0x01], ramp):
        for sz in (0, 1, 2, 3, 4, 5, 6, 7, 8, 49, 50):
            for spaced in (True, False):
                out.append((data, sz, spaced, ref_hex(data, sz, spaced)))
    return out


def render_i2c():
    lines = [
        "/* Generated by scripts/gen_lab_vectors.py — do not edit.",
        " * SPEC-DERIVED, NOT A RECORDING OF A REAL BUS (ADR-0023 §5): sweeps over made-up answers",
        " * (ACK, timeout, NACK per address), fingerprints (FNV-1a), the refusal table and hex text,",
        " * each with the result an independent Python implementation computes.",
        " * Test infrastructure only. */",
        "/* clang-format off */",
        "#pragma once",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "/* status: 0 OK, 1 IO (errors ended the sweep) */",
        "typedef struct {",
        "    const char *name;",
        "    const uint8_t *acks;",
        "    uint8_t n_acks;",
        "    const uint8_t *errs;",
        "    uint8_t n_errs;",
        "    const uint8_t *found;",
        "    uint8_t n_found;",
        "    uint8_t errors;",
        "    uint8_t status;",
        "    uint8_t probes;",
        "} fake_lab_sweep_vec_t;",
        "",
        "/* kind 'W' (a write: addr, reg, reg16, data) or 'D' (a dump: addr) */",
        "typedef struct {",
        "    char kind;",
        "    uint8_t addr;",
        "    uint16_t reg;",
        "    uint8_t reg16;",
        "    const uint8_t *data;",
        "    uint8_t n;",
        "    uint32_t fp;",
        "} fake_lab_fp_vec_t;",
        "",
        "/* EEPROM addresses 0x50 and 0x51; expect 0 none, 1 module eeprom, 2 nfc reader */",
        "typedef struct {",
        "    uint8_t addr;",
        "    uint8_t wn;",
        "    uint8_t rn;",
        "    uint8_t nfc;",
        "    uint8_t expect;",
        "} fake_lab_refusal_vec_t;",
        "",
        "typedef struct {",
        "    const uint8_t *data;",
        "    uint8_t n;",
        "    uint8_t sz;",
        "    uint8_t spaced;",
        "    const char *text;",
        "} fake_lab_hex_vec_t;",
        "",
    ]

    def arr(name, vals):
        lines.append("static const uint8_t %s[] = {%s};" % (name, ",".join("0x%02x" % v
                                                                          for v in vals)))

    sw = sweep_vectors()
    for i, (name, acks, errs) in enumerate(sw):
        found, errors, status, probes = ref_sweep(acks, errs)
        for tag, vals in (("acks", sorted(acks)), ("errs", sorted(errs)), ("found", found)):
            if vals:
                arr("fake_lab_sw_%d_%s" % (i, tag), vals)
    lines.append("")
    lines.append("static const fake_lab_sweep_vec_t fake_lab_sweep_vecs[] = {")
    for i, (name, acks, errs) in enumerate(sw):
        found, errors, status, probes = ref_sweep(acks, errs)

        def ref(tag, vals, i=i):
            return "fake_lab_sw_%d_%s" % (i, tag) if vals else "NULL"

        lines.append('    {"%s", %s, %d, %s, %d, %s, %d, %d, %d, %d},' % (
            name, ref("acks", acks), len(acks), ref("errs", errs), len(errs),
            ref("found", found), len(found), errors, status, probes))
    lines.append("};")
    lines.append("#define FAKE_LAB_SWEEP_VECS (sizeof(fake_lab_sweep_vecs) / "
                 "sizeof(fake_lab_sweep_vecs[0]))")
    lines.append("")

    fps = fp_vectors()
    for i, (kind, addr, reg, reg16, data, fp) in enumerate(fps):
        if data:
            arr("fake_lab_fp_%d" % i, data)
    lines.append("")
    lines.append("static const fake_lab_fp_vec_t fake_lab_fp_vecs[] = {")
    for i, (kind, addr, reg, reg16, data, fp) in enumerate(fps):
        lines.append("    {'%s', 0x%02x, 0x%04x, %d, %s, %d, %du}," % (
            kind, addr, reg, int(reg16), "fake_lab_fp_%d" % i if data else "NULL", len(data), fp))
    lines.append("};")
    lines.append("#define FAKE_LAB_FP_VECS (sizeof(fake_lab_fp_vecs) / sizeof(fake_lab_fp_vecs[0]))")
    lines.append("")

    lines.append("static const fake_lab_refusal_vec_t fake_lab_refusal_vecs[] = {")
    for addr, wn, rn, nfc, expect in refusal_vectors():
        lines.append("    {0x%02x, %d, %d, %d, %d}," % (addr, wn, rn, int(nfc), expect))
    lines.append("};")
    lines.append("#define FAKE_LAB_REFUSAL_VECS (sizeof(fake_lab_refusal_vecs) / "
                 "sizeof(fake_lab_refusal_vecs[0]))")
    lines.append("")

    hv = hex_vectors()
    seen = {}
    for data, sz, spaced, text in hv:
        key = tuple(data)
        if key not in seen:
            seen[key] = "fake_lab_hexdata_%d" % len(seen)
            if data:
                arr(seen[key], data)
    lines.append("")
    lines.append("static const fake_lab_hex_vec_t fake_lab_hex_vecs[] = {")
    for data, sz, spaced, text in hv:
        lines.append('    {%s, %d, %d, %d, "%s"},' % (
            seen[tuple(data)] if data else "NULL", len(data), sz, int(spaced), text))
    lines.append("};")
    lines.append("#define FAKE_LAB_HEX_VECS (sizeof(fake_lab_hex_vecs) / sizeof(fake_lab_hex_vecs[0]))")
    lines.append("")
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args(argv)
    outputs = ((OUT, render()), (OUT_I2C, render_i2c()))
    if args.check:
        stale = [path for path, text in outputs if not path.exists() or path.read_text() != text]
        for path in stale:
            print("STALE: %s — run scripts/gen_lab_vectors.py" % path, file=sys.stderr)
        return 1 if stale else 0
    for path, text in outputs:
        path.write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
