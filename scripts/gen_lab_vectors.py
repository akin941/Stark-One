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

The UART vectors (STARK-0705, ADR-0023 §7) are as independent: ref_autobaud() rebuilds the edges
of a stream bit by bit (uart_edges()) and restates the estimator of lab_autobaud.h with its own
arithmetic, ref_loop() judges a loopback byte by byte, ref_ring() models the ring as a list,
ref_parse_send() is the grammar of the send file, and the CRC-32 is zlib's.

The SPI vectors (STARK-0706, ADR-0023 §6) are as independent: ref_spi_judge() restates which slot
may be probed and why not (MOD_SPI and no function bit, the chip select wired, the module known),
ref_fp_spi() is the textbook FNV-1a over the byte string the request's fingerprint is defined on,
and ref_spi_flash() / ref_spi_echo() are the two test-only SPI devices of test/fake/fake_lab.h
written out byte by byte.

Usage:
  gen_lab_vectors.py            write test/fake/fake_lab_vectors.h, fake_lab_i2c_vectors.h,
                                fake_lab_uart_vectors.h and fake_lab_spi_vectors.h
  gen_lab_vectors.py --check    exit 1 if a committed file differs (CI lint)
"""
import argparse
import sys
import zlib
from fractions import Fraction
from math import floor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "test/fake/fake_lab_vectors.h"
OUT_I2C = ROOT / "test/fake/fake_lab_i2c_vectors.h"
OUT_UART = ROOT / "test/fake/fake_lab_uart_vectors.h"
OUT_SPI = ROOT / "test/fake/fake_lab_spi_vectors.h"

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


# ---- the UART tool (STARK-0705, ADR-0023 §7) -----------------------------------------------
# Everything below is written from the definitions in components/stark_lab/lab_uart.h and
# lab_autobaud.h, not from the firmware's code: the baud and format lists, the wire time, a byte
# ring modelled as a plain list, the loopback judgement byte by byte, hex and ASCII rows, the send
# file's grammar, the log names, zlib's CRC-32, and an auto-baud that rebuilds the edges of a
# stream bit by bit and restates the estimator with its own arithmetic.

UART_BAUDS = [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600]
UART_FMTS = [("8N1", 8, "N", 1), ("8E1", 8, "E", 1), ("8O1", 8, "O", 1), ("8N2", 8, "N", 2),
             ("7E1", 7, "E", 1)]
LOOP_OK, LOOP_MISMATCH, LOOP_LINE_ERRORS, LOOP_NO_DATA = 0, 1, 2, 3
FILE_OK, FILE_EMPTY, FILE_TOO_BIG, FILE_BAD = 0, 1, 2, 3
BAUD_OK, BAUD_TOO_FEW, BAUD_NO_PULSE, BAUD_UNKNOWN, BAUD_INVALID = 0, 1, 2, 3, 4
SEND_MAX = 32
FILE_MAX = 1024
LOOP_LEN = 64
AUTOBAUD_MIN_EDGES = 16
SCENE_LINES = 50


def uart_bits(fmt):
    _, data, parity, stop = UART_FMTS[fmt]
    return 1 + data + (0 if parity == "N" else 1) + stop


def ref_wire_ms(n, baud, fmt):
    bits = n * uart_bits(fmt)
    return -(-bits * 1000 // baud)  # rounded up


def ref_window_ms(baud, fmt):
    return 2 * ref_wire_ms(LOOP_LEN, baud, fmt) + 20


def wire_vectors():
    out = []
    for fmt in range(len(UART_FMTS)):
        for baud in (1200, 9600, 115200, 921600):
            for n in (1, 32, 64):
                out.append((n, baud, fmt, ref_wire_ms(n, baud, fmt), ref_window_ms(baud, fmt)))
    return out


def ref_ring(cap, total, chunk):
    """The byte `k` pushed is k & 0xFF; the ring keeps the last `cap` of them."""
    seq = [k & 0xFF for k in range(total)]
    kept = seq[-cap:] if total > cap else seq
    return len(kept), total - len(kept), kept


def ring_vectors():
    out = []
    for cap, total, chunk, skip, take in (
            (8, 0, 1, 0, 8), (8, 5, 1, 0, 8), (8, 8, 8, 0, 8), (8, 9, 1, 0, 8), (8, 9, 9, 3, 4),
            (8, 20, 3, 2, 5), (8, 20, 20, 0, 100), (8, 1000, 7, 7, 1), (8, 16, 4, 8, 2),
            (4096, 4095, 64, 0, 4096), (4096, 4096, 64, 0, 4096), (4096, 4097, 64, 1, 4096),
            (4096, 9000, 64, 100, 300), (4096, 9000, 256, 4090, 100)):
        count, lost, kept = ref_ring(cap, total, chunk)
        peek = kept[skip:skip + take]
        out.append((cap, total, chunk, skip, take, count, lost, zlib.crc32(bytes(kept)),
                    len(peek), zlib.crc32(bytes(peek))))
    return out


def ref_loop(rx, sent, framing, parity, overrun):
    """(status, received, errors, first) for what came back for the first `sent` pattern bytes."""
    want = list(range(LOOP_LEN))[:sent]
    line = framing + parity + overrun
    if not rx:
        return LOOP_NO_DATA, 0, sent + line, 0 if sent else -1
    bad, first = 0, -1
    for i in range(max(len(rx), len(want))):
        if i >= len(rx) or i >= len(want) or rx[i] != want[i]:
            bad += 1
            if first < 0:
                first = i
    if bad:
        return LOOP_MISMATCH, len(rx), bad + line, first
    if line:
        return LOOP_LINE_ERRORS, len(rx), line, -1
    return LOOP_OK, len(rx), 0, -1


def loop_vectors():
    p = list(range(LOOP_LEN))
    flip = lambda i, bit: [b ^ (1 << bit) if k == i else b for k, b in enumerate(p)]  # noqa: E731
    cases = [
        ("exact", p, 64, 0, 0, 0),
        ("nothing", [], 64, 0, 0, 0),
        ("nothing_with_line_errors", [], 64, 3, 0, 1),
        ("one_bit_at_0", flip(0, 0), 64, 0, 0, 0),
        ("one_bit_at_31", flip(31, 3), 64, 0, 0, 0),
        ("one_bit_at_63", flip(63, 7), 64, 0, 0, 0),
        ("a_byte_dropped_in_the_middle", p[:20] + p[21:], 64, 0, 0, 0),
        ("the_last_dropped", p[:63], 64, 0, 0, 0),
        ("truncated_at_40", p[:40], 64, 0, 0, 0),
        ("one_byte", p[:1], 64, 0, 0, 0),
        ("an_extra_byte", p + [0x55], 64, 0, 0, 0),
        ("the_stream_shifted_by_one", p[1:] + [0], 64, 0, 0, 0),
        ("all_zero", [0] * 64, 64, 0, 0, 0),
        ("garbage", [(b * 5 + 1) & 0xFF for b in p], 64, 64, 0, 0),
        ("match_with_framing_errors", p, 64, 2, 0, 0),
        ("match_with_parity_errors", p, 64, 0, 5, 0),
        ("match_with_an_overrun", p, 64, 0, 0, 1),
        ("mismatch_with_line_errors", flip(10, 1), 64, 1, 1, 1),
        ("a_shorter_pattern", p[:32], 32, 0, 0, 0),
        ("a_shorter_pattern_cut", p[:31], 32, 0, 0, 0),
        ("nothing_of_nothing", [], 0, 0, 0, 0),
    ]
    return [(name, rx, sent, f, pa, o) + ref_loop(rx, sent, f, pa, o)
            for name, rx, sent, f, pa, o in cases]


def ref_hex_row(data, sz):
    if sz == 0:
        return ""
    out = ""
    for i, b in enumerate(data):
        piece = ("%02x" % b) if i == 0 else (" %02x" % b)
        if len(out) + len(piece) + 1 > sz:
            break
        out += piece
    return out


def ref_ascii_row(data, sz):
    if sz == 0:
        return ""
    return "".join(chr(b) if 0x20 <= b <= 0x7E else "." for b in data)[:sz - 1]


def row_vectors():
    out = []
    for r in range(16):
        data = list(range(16 * r, 16 * r + 16))
        out.append((data, 64, ref_hex_row(data, 64), ref_ascii_row(data, 64)))
    data = list(range(0x30, 0x40))
    for sz in (0, 1, 2, 3, 4, 5, 6, 46, 47, 48, 49, 50):
        out.append((data, sz, ref_hex_row(data, sz), ref_ascii_row(data, sz)))
    out.append(([], 8, "", ""))
    return out


def ref_parse_send(text):
    """(status, bytes) of the send file: `hex:` and pairs, or printable ASCII."""
    if len(text) > FILE_MAX:
        return FILE_TOO_BIG, []
    if text.startswith(b"hex:"):
        body = text[4:]
        toks, out, i = body, [], 0
        while i < len(toks):
            c = toks[i]
            if c in b" \t\r\n":
                i += 1
                continue
            pair = toks[i:i + 2]
            if len(pair) < 2 or any(ch not in b"0123456789abcdefABCDEF" for ch in pair):
                return FILE_BAD, []
            if len(out) == SEND_MAX:
                return FILE_TOO_BIG, []
            out.append(int(pair.decode(), 16))
            i += 2
        return (FILE_OK, out) if out else (FILE_EMPTY, [])
    body = text
    if body.endswith(b"\r\n"):
        body = body[:-2]
    elif body.endswith(b"\n"):
        body = body[:-1]
    if any(not 0x20 <= b <= 0x7E for b in body):
        return FILE_BAD, []
    if not body:
        return FILE_EMPTY, []
    if len(body) > SEND_MAX:
        return FILE_TOO_BIG, []
    return FILE_OK, list(body)


def file_vectors():
    texts = [
        b"AT", b"AT\r\n", b"?\n", b"hello\r\n", b"hello\n\n", b"a\tb", b"a\x80b", b"a\x7fb",
        b"", b"\n", b"\r\n", b"\r", b" ", b"x" * 32, b"x" * 33, b"x" * 32 + b"\n",
        b"x" * 33 + b"\r\n", b"x" * 1024, b"x" * 1025, b"hexagon", b"HEX:41", b"hex", b"hex:",
        b"hex:41 54 0d 0a", b"hex:4154", b"hex:41  54\n", b"hex:\n41\t54\r\n", b"hex:4A4b",
        b"hex:4", b"hex:4g", b"hex:0x41", b"hex: 41 5", b"hex:41 5 4", b"hex:   ",
        b"hex:" + b"41 " * 32, b"hex:" + b"41 " * 33, b"hex:" + b"41" * 32,
        b"hex:41" + b" " * 1020, b"hex:41" + b" " * 1021, b"hex:41,42", b"hex:\x80",
    ]
    return [(t, ) + ref_parse_send(t) for t in texts]


def ref_log_name(name):
    if len(name) == 13 and name.startswith("uart_") and name.endswith(".log") \
            and name[5:9].isdigit() and name[5:9].isascii():
        return True, int(name[5:9])
    return False, 0


def name_vectors():
    parse = ["uart_0001.log", "uart_9999.log", "uart_0000.log", "uart_0123.log", "uart_001.log",
             "uart_00001.log", "uart_0001.txt", "Uart_0001.log", "uart_000a.log", "uart_0001.log ",
             "uart-0001.log", "uart_0001log", "", "uart_.log", "xuart_0001.log"]
    nxt = []
    for highest in (0, 1, 8, 9, 98, 99, 998, 999, 1234, 9997, 9998, 9999):
        nxt.append((highest, highest >= 9999, "" if highest >= 9999 else "uart_%04d.log"
                    % (highest + 1)))
    return [(p,) + ref_log_name(p) for p in parse], nxt


def crc_vectors():
    out = []
    for data in (b"", b"a", b"123456789", bytes(range(256)), b"stark uart scene line 00\r\n",
                 b"\x00" * 100, b"\xff" * 33):
        out.append((data, zlib.crc32(data)))
    return out


def scene_text():
    return b"".join(b"stark uart scene line %02d\r\n" % i for i in range(SCENE_LINES))


def scene_binary():
    return bytes((j * 7 + 3) & 0xFF for j in range(1024))


def frame_bits(byte, fmt):
    """One character as line levels: start, data (least significant first), parity, stop."""
    _, data, parity, stop = UART_FMTS[fmt]
    bits, ones = [0], 0
    for d in range(data):
        b = (byte >> d) & 1
        ones += b
        bits.append(b)
    if parity == "E":
        bits.append(ones & 1)
    elif parity == "O":
        bits.append(1 - (ones & 1))
    return bits + [1] * stop


def uart_edges(data, baud, fmt, base, jitter_ticks=0, seed=1, gap_bits=0, spikes=0):
    """The stamps (32-bit wrapped, in a clock of TICK_HZ) of the edges of `data` sent back to back
    (`gap_bits` idle bits after each character), bit boundaries computed from the start; edges
    displaced by up to +-jitter_ticks and kept in order; `spikes` 0.3 us low pulses are put into
    the idle line after every character that is followed by a gap of at least 8 bits."""
    rng = Rng(seed)
    edges, level, m = [], 1, 0
    for byte in data:
        for lv in frame_bits(byte, fmt):
            if lv != level:
                edges.append(m * TICK_HZ // baud)
                level = lv
            m += 1
        if gap_bits:
            if spikes and gap_bits >= 8:
                t = m * TICK_HZ // baud + (gap_bits * TICK_HZ // baud) // 2
                edges += [t, t + 72]
            m += gap_bits
    out, prev = [], None
    for t in edges:
        t2 = t + (rng.between(-jitter_ticks, jitter_ticks) if jitter_ticks else 0)
        if prev is not None and t2 <= prev:
            t2 = prev + 1
        out.append(t2)
        prev = t2
    return [(t + base) & 0xFFFFFFFF for t in out]


def ref_autobaud(ts, tick_hz):
    """(status, baud, raw, bit_ticks) from the stamps, the rule of lab_autobaud.h restated."""
    n = len(ts)
    if tick_hz == 0 or n > 2048:
        return BAUD_INVALID, 0, 0, 0
    if n < AUTOBAUD_MIN_EDGES:
        return BAUD_TOO_FEW, 0, 0, 0
    glitch = max(tick_hz // 1_000_000, 1)
    widths = [(b - a) & 0xFFFFFFFF for a, b in zip(ts, ts[1:])]
    if any(w >= 0x80000000 for w in widths):
        return BAUD_INVALID, 0, 0, 0
    real = [w for w in widths if w >= glitch]
    if not real:
        return BAUD_NO_PULSE, 0, 0, 0
    m = min(real)
    raw = (tick_hz + m // 2) // m
    best = min(UART_BAUDS, key=lambda b: (abs(m * b - tick_hz), b))
    if abs(m * best - tick_hz) * 100 > 35 * tick_hz:
        return BAUD_UNKNOWN, 0, raw, 0
    bit = (tick_hz + best // 2) // best
    for _ in range(2):
        sw = sk = 0
        for w in real:
            if w <= 12 * bit:
                sw += w
                sk += max(1, (w + bit // 2) // bit)
        bit = max(1, (sw + sk // 2) // sk)
    raw = (tick_hz + bit // 2) // bit
    for b in UART_BAUDS:
        if abs(raw - b) * 100 <= 3 * b:
            return BAUD_OK, b, raw, bit
    return BAUD_UNKNOWN, 0, raw, bit


SAMPLE = b"UART autobaud sample 0123456789\r\n"


def autobaud_vectors():
    out = []
    seq = [100]

    def add(name, ts, nominal=0, expect=None, tick=TICK_HZ):
        status, baud, raw, bit = ref_autobaud(ts, tick)
        if nominal:
            assert (status, baud) == (BAUD_OK, nominal), "%s: the reference says %s %s" % (
                name, status, baud)
        if expect is not None:
            assert status == expect, "%s: the reference says %s, wanted %s" % (name, status, expect)
        out.append((name, tick, ts, nominal, status, baud, raw, bit))

    def next_seed():
        seq[0] += 1
        return seq[0]

    def jit(baud):
        return min(TICK_HZ // 1_000_000, TICK_HZ // baud // 10)  # +-1 us, or a tenth of a bit

    for baud in UART_BAUDS:
        add("text_%d_8N1_clean" % baud, uart_edges(SAMPLE, baud, 0, 100_000), baud)
        add("text_%d_8N1_jitter" % baud, uart_edges(SAMPLE, baud, 0, 0xFFFFF000 if baud == 9600
                                                    else 1_000_000, jit(baud), next_seed()), baud)
    for fmt in range(1, len(UART_FMTS)):
        for baud in (9600, 115200):
            add("text_%d_%s_jitter" % (baud, UART_FMTS[fmt][0]),
                uart_edges(SAMPLE, baud, fmt, 2_000_000, jit(baud), next_seed()), baud)
    for name, data in (("alternating_0x55", [0x55] * 20), ("all_ones_0xff", [0xFF] * 24),
                       ("all_zero_0x00", [0x00] * 24), ("runs_f0_0f", [0xF0, 0x0F] * 12),
                       ("ramp", list(range(0x20, 0x40)) * 2), ("high_bytes", [0x80 | b for b
                                                                              in range(24)])):
        add("pattern_%s_115200" % name, uart_edges(bytes(data), 115200, 0, 500_000, jit(115200),
                                                   next_seed()), 115200)
        add("pattern_%s_9600" % name, uart_edges(bytes(data), 9600, 0, 500_000, jit(9600),
                                                 next_seed()), 9600)
    for baud in (1200, 38400, 115200):
        add("idle_gaps_%d" % baud, uart_edges(SAMPLE[:20], baud, 0, 700_000, jit(baud),
                                              next_seed(), gap_bits=40), baud)
        add("spikes_in_the_idle_%d" % baud, uart_edges(SAMPLE[:20], baud, 0, 700_000, 0,
                                                       next_seed(), gap_bits=40, spikes=1), baud)
    # a line that only chatters: every pulse a glitch, nothing a bit
    add("only_glitches", [1000 + 100 * i for i in range(40)], expect=BAUD_NO_PULSE)
    add("only_zero_length_steps", [5000] * 20, expect=BAUD_NO_PULSE)
    add("no_edges", [], expect=BAUD_TOO_FEW)
    add("one_edge", [777], expect=BAUD_TOO_FEW)
    full = uart_edges(SAMPLE, 9600, 0, 100)
    add("fifteen_edges", full[:15], expect=BAUD_TOO_FEW)
    add("sixteen_edges", full[:16], 9600)
    add("steps_that_go_backwards", [1000 + 5000 * i for i in range(10)] + [500] + [
        1000 + 5000 * i for i in range(10)], expect=BAUD_INVALID)
    # a train of 1-bit pulses whose bit time is a little off a listed baud
    for baud in (9600, 115200):
        for pct, ok in ((-2.9, True), (2.9, True), (-3.1, False), (3.1, False)):
            hz = baud * (1 + pct / 100)
            period = TICK_HZ / hz
            ts = [int(i * period) for i in range(40)]
            add("train_%d_%+.1fpct" % (baud, pct), ts, baud if ok else 0,
                None if ok else BAUD_UNKNOWN)
    for odd in (300, 14400, 28800, 76800):
        ts = [int(i * TICK_HZ / odd) for i in range(40)]
        add("train_%d_is_no_listed_baud" % odd, ts, expect=BAUD_UNKNOWN)
    add("train_2_mbaud_is_all_glitch", [int(i * TICK_HZ / 2_000_000) for i in range(40)],
        expect=BAUD_NO_PULSE)
    add("a_slow_clock", [i * 1000 for i in range(20)], tick=1_000_000, expect=BAUD_UNKNOWN)
    add("a_clock_under_1_mhz", [i * 500 for i in range(20)], tick=500_000, expect=BAUD_UNKNOWN)
    add("no_clock", [i * 1000 for i in range(20)], tick=0, expect=BAUD_INVALID)
    add("too_many_edges", [i * 2500 for i in range(2049)], expect=BAUD_INVALID)
    return out


def render_uart():
    L = [
        "/* Generated by scripts/gen_lab_vectors.py — do not edit.",
        " * SPEC-DERIVED, NOT A RECORDING OF A REAL LINE (ADR-0023 §7): wire times, ring sequences,",
        " * loopback judgements, hex and ASCII rows, send files, log names, CRC-32 and the edge",
        " * stamps of made-up streams at every baud and format with the baud an independent Python",
        " * implementation names. Test infrastructure only. */",
        "/* clang-format off */",
        "#pragma once",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "typedef struct { uint16_t n; uint32_t baud; uint8_t fmt; uint32_t ms; uint32_t window; }"
        " fake_lab_uart_wire_vec_t;",
        "/* bytes k & 0xFF pushed in pieces of `chunk` into a ring of `cap`; then `take` bytes are"
        " peeked from `skip` */",
        "typedef struct { uint16_t cap; uint32_t total; uint16_t chunk; uint16_t skip; uint16_t take;"
        " uint16_t count; uint32_t lost; uint32_t crc; uint16_t peek_n; uint32_t peek_crc; }"
        " fake_lab_uart_ring_vec_t;",
        "/* status: 0 OK, 1 MISMATCH, 2 LINE_ERRORS, 3 NO_DATA */",
        "typedef struct { const char *name; const uint8_t *rx; uint8_t n_rx; uint8_t sent;"
        " uint8_t framing; uint8_t parity; uint8_t overrun; uint8_t status; uint8_t received;"
        " uint8_t errors; int8_t first; } fake_lab_uart_loop_vec_t;",
        "typedef struct { const uint8_t *data; uint8_t n; uint8_t sz; const char *hex;"
        " const char *ascii; } fake_lab_uart_row_vec_t;",
        "/* status: 0 OK, 1 EMPTY, 2 TOO_BIG, 3 BAD */",
        "typedef struct { const uint8_t *text; uint16_t n; uint8_t status; const uint8_t *out;"
        " uint8_t n_out; } fake_lab_uart_file_vec_t;",
        "typedef struct { const char *name; uint8_t ok; uint16_t number; }"
        " fake_lab_uart_parse_vec_t;",
        "typedef struct { uint16_t highest; uint8_t no_space; const char *name; }"
        " fake_lab_uart_next_vec_t;",
        "typedef struct { const uint8_t *data; uint16_t n; uint32_t crc; } fake_lab_uart_crc_vec_t;",
        "/* status: 0 OK, 1 TOO_FEW, 2 NO_PULSE, 3 UNKNOWN, 4 INVALID; nominal: the baud the stream"
        " was made at (0: none) */",
        "typedef struct { const char *name; uint32_t tick; const uint32_t *ts; uint16_t n;"
        " uint32_t nominal; uint8_t status; uint32_t baud; uint32_t raw; uint32_t bit;"
        " } fake_lab_uart_autobaud_vec_t;",
        "",
    ]

    def arr(name, vals, fmt="0x%02x", ctype="uint8_t"):
        L.append("static const %s %s[] = {%s};" % (ctype, name, ",".join(fmt % v for v in vals)))

    def cstr(s):
        return '"%s"' % s.replace("\\", "\\\\").replace('"', '\\"')

    L.append("#define FAKE_LAB_UART_TEXT_SCENE_CRC 0x%08xu" % zlib.crc32(scene_text()))
    L.append("#define FAKE_LAB_UART_BINARY_SCENE_CRC 0x%08xu" % zlib.crc32(scene_binary()))
    L.append("")
    L.append("static const fake_lab_uart_wire_vec_t fake_lab_uart_wire_vecs[] = {")
    for n, baud, fmt, ms, win in wire_vectors():
        L.append("    {%d, %d, %d, %d, %d}," % (n, baud, fmt, ms, win))
    L.append("};")
    L.append("#define FAKE_LAB_UART_WIRE_VECS (sizeof(fake_lab_uart_wire_vecs) / "
             "sizeof(fake_lab_uart_wire_vecs[0]))")
    L.append("")
    L.append("static const fake_lab_uart_ring_vec_t fake_lab_uart_ring_vecs[] = {")
    for cap, total, chunk, skip, take, count, lost, crc, pn, pcrc in ring_vectors():
        L.append("    {%d, %d, %d, %d, %d, %d, %d, 0x%08xu, %d, 0x%08xu}," % (
            cap, total, chunk, skip, take, count, lost, crc, pn, pcrc))
    L.append("};")
    L.append("#define FAKE_LAB_UART_RING_VECS (sizeof(fake_lab_uart_ring_vecs) / "
             "sizeof(fake_lab_uart_ring_vecs[0]))")
    L.append("")
    lv = loop_vectors()
    for i, v in enumerate(lv):
        if v[1]:
            arr("fake_lab_uart_loop_%d" % i, v[1])
    L.append("static const fake_lab_uart_loop_vec_t fake_lab_uart_loop_vecs[] = {")
    for i, (name, rx, sent, f, pa, o, status, received, errors, first) in enumerate(lv):
        L.append('    {"%s", %s, %d, %d, %d, %d, %d, %d, %d, %d, %d},' % (
            name, "fake_lab_uart_loop_%d" % i if rx else "NULL", len(rx), sent, f, pa, o, status,
            received, errors, first))
    L.append("};")
    L.append("#define FAKE_LAB_UART_LOOP_VECS (sizeof(fake_lab_uart_loop_vecs) / "
             "sizeof(fake_lab_uart_loop_vecs[0]))")
    L.append("")
    rv = row_vectors()
    for i, (data, sz, hx, asc) in enumerate(rv):
        if data:
            arr("fake_lab_uart_row_%d" % i, data)
    L.append("static const fake_lab_uart_row_vec_t fake_lab_uart_row_vecs[] = {")
    for i, (data, sz, hx, asc) in enumerate(rv):
        L.append("    {%s, %d, %d, %s, %s}," % ("fake_lab_uart_row_%d" % i if data else "NULL",
                                                len(data), sz, cstr(hx), cstr(asc)))
    L.append("};")
    L.append("#define FAKE_LAB_UART_ROW_VECS (sizeof(fake_lab_uart_row_vecs) / "
             "sizeof(fake_lab_uart_row_vecs[0]))")
    L.append("")
    fv = file_vectors()
    for i, (text, status, out) in enumerate(fv):
        if text:
            arr("fake_lab_uart_file_%d" % i, list(text))
        if out:
            arr("fake_lab_uart_file_%d_out" % i, out)
    L.append("static const fake_lab_uart_file_vec_t fake_lab_uart_file_vecs[] = {")
    for i, (text, status, out) in enumerate(fv):
        L.append("    {%s, %d, %d, %s, %d}," % (
            "fake_lab_uart_file_%d" % i if text else "NULL", len(text), status,
            "fake_lab_uart_file_%d_out" % i if out else "NULL", len(out)))
    L.append("};")
    L.append("#define FAKE_LAB_UART_FILE_VECS (sizeof(fake_lab_uart_file_vecs) / "
             "sizeof(fake_lab_uart_file_vecs[0]))")
    L.append("")
    parse, nxt = name_vectors()
    L.append("static const fake_lab_uart_parse_vec_t fake_lab_uart_parse_vecs[] = {")
    for name, ok, num in parse:
        L.append("    {%s, %d, %d}," % (cstr(name), int(ok), num))
    L.append("};")
    L.append("#define FAKE_LAB_UART_PARSE_VECS (sizeof(fake_lab_uart_parse_vecs) / "
             "sizeof(fake_lab_uart_parse_vecs[0]))")
    L.append("static const fake_lab_uart_next_vec_t fake_lab_uart_next_vecs[] = {")
    for highest, no_space, name in nxt:
        L.append("    {%d, %d, %s}," % (highest, int(no_space), cstr(name)))
    L.append("};")
    L.append("#define FAKE_LAB_UART_NEXT_VECS (sizeof(fake_lab_uart_next_vecs) / "
             "sizeof(fake_lab_uart_next_vecs[0]))")
    L.append("")
    cv = crc_vectors()
    for i, (data, crc) in enumerate(cv):
        if data:
            arr("fake_lab_uart_crc_%d" % i, list(data))
    L.append("static const fake_lab_uart_crc_vec_t fake_lab_uart_crc_vecs[] = {")
    for i, (data, crc) in enumerate(cv):
        L.append("    {%s, %d, 0x%08xu}," % ("fake_lab_uart_crc_%d" % i if data else "NULL",
                                              len(data), crc))
    L.append("};")
    L.append("#define FAKE_LAB_UART_CRC_VECS (sizeof(fake_lab_uart_crc_vecs) / "
             "sizeof(fake_lab_uart_crc_vecs[0]))")
    L.append("")
    av = autobaud_vectors()
    for i, (name, tick, ts, nominal, status, baud, raw, bit) in enumerate(av):
        if ts:
            arr("fake_lab_uart_ab_%d" % i, ts, "0x%08xu", "uint32_t")
    L.append("static const fake_lab_uart_autobaud_vec_t fake_lab_uart_autobaud_vecs[] = {")
    for i, (name, tick, ts, nominal, status, baud, raw, bit) in enumerate(av):
        L.append('    {"%s", %d, %s, %d, %d, %d, %d, %d, %d},' % (
            name, tick, "fake_lab_uart_ab_%d" % i if ts else "NULL", len(ts), nominal, status,
            baud, raw, bit))
    L.append("};")
    L.append("#define FAKE_LAB_UART_AUTOBAUD_VECS (sizeof(fake_lab_uart_autobaud_vecs) / "
             "sizeof(fake_lab_uart_autobaud_vecs[0]))")
    L.append("")
    return "\n".join(L)


# ---- the SPI probe (STARK-0706, ADR-0023 §6) -----------------------------------------------
# Written from the rules in components/stark_lab/lab_spi.h and test/fake/fake_lab.h, not from the
# firmware's code. Module table states in the order of stark_module.h: ABSENT, KNOWN, GENERIC,
# CONFLICT.

SPI_ABSENT, SPI_KNOWN, SPI_GENERIC, SPI_CONFLICT = 0, 1, 2, 3
CAP_IR_TX, CAP_IR_RX, CAP_NFC, CAP_SPI, CAP_IRQ, CAP_SUBGHZ = 1, 2, 4, 8, 16, 32
SPI_DEV_NONE, SPI_DEV_FLASH, SPI_DEV_ECHO = 0, 1, 2


def ref_spi_judge(cs, state, declared, unsupported):
    """(eligible, why): a wired slot that reads KNOWN and declares MOD_SPI and no function bit."""
    if not cs:
        return False, "no chip select"
    if state == SPI_ABSENT:
        return False, "no module"
    if state == SPI_GENERIC:
        return False, "unsupported module" if unsupported else "generic module"
    if state == SPI_CONFLICT:
        return False, "conflict"
    extra = declared & ~(CAP_SPI | CAP_IRQ) & 0xFFFFFFFF
    if declared & CAP_SPI and not extra:
        return True, ""
    if extra:
        if declared & CAP_SUBGHZ:
            owner = "Sub-GHz"
        elif declared & CAP_NFC:
            owner = "NFC"
        elif declared & (CAP_IR_TX | CAP_IR_RX):
            owner = "IR"
        else:
            owner = "unknown"
        return False, "owned by " + owner
    return False, "no SPI declared"


def ref_fp_spi(slot, hz, mode, tx, mod_id, rev, declared):
    def le(v, n):
        return bytes((v >> (8 * i)) & 0xFF for i in range(n))
    return ref_fnv1a(b"S" + bytes([slot]) + le(hz, 4) + bytes([mode, len(tx)]) + bytes(tx)
                     + le(mod_id, 2) + bytes([rev]) + le(declared, 4))


def ref_spi_flash(tx):
    """MISO of a JEDEC flash class device: zero during the command, then what it returns."""
    n = len(tx)
    rx = [0] * n
    if tx[0] == 0x9F:
        for i, b in enumerate((0xEF, 0x40, 0x18)):
            if i + 1 < n:
                rx[i + 1] = b
    elif tx[0] == 0x03 and n > 4:
        addr = (tx[1] << 16) | (tx[2] << 8) | tx[3]
        for i in range(4, n):
            rx[i] = (addr + i - 4) & 0xFF
    return rx


def ref_spi_echo(tx):
    return [0] + list(tx[:-1])


def spi_judge_vectors():
    masks = [0, CAP_SPI, CAP_IRQ, CAP_SPI | CAP_IRQ, CAP_NFC, CAP_NFC | CAP_SPI,
             CAP_NFC | CAP_IRQ, CAP_NFC | CAP_SPI | CAP_IRQ, CAP_SUBGHZ,
             CAP_SUBGHZ | CAP_SPI | CAP_IRQ, CAP_SUBGHZ | CAP_SPI, CAP_IR_TX, CAP_IR_RX,
             CAP_IR_TX | CAP_SPI, CAP_IR_RX | CAP_SPI | CAP_IRQ, 0x40, 0x40 | CAP_SPI,
             0x80000000, 0x80000000 | CAP_SPI, 0x80000000 | CAP_SPI | CAP_IRQ, 0xFFFFFFFF,
             CAP_SUBGHZ | CAP_NFC | CAP_SPI]
    out = []
    for cs in (True, False):
        for state in (SPI_ABSENT, SPI_KNOWN, SPI_GENERIC, SPI_CONFLICT):
            for declared in masks:
                for unsupported in ((False, True) if state == SPI_GENERIC else (False,)):
                    ok, why = ref_spi_judge(cs, state, declared, unsupported)
                    out.append((cs, state, declared, unsupported, ok, why))
    return out


def spi_fp_vectors():
    rows = []
    jedec = [0x9F, 0, 0, 0]
    for slot, hz, mode, tx, ident in (
            (0, 1000000, 0, jedec, (0x0B01, 1, 0x08)),
            (1, 1000000, 0, jedec, (0x0B01, 1, 0x08)),
            (0, 100000, 0, jedec, (0x0B01, 1, 0x08)),
            (0, 4000000, 0, jedec, (0x0B01, 1, 0x08)),
            (0, 1000000, 3, jedec, (0x0B01, 1, 0x08)),
            (0, 1000000, 0, [0x9F, 0, 0, 1], (0x0B01, 1, 0x08)),
            (0, 1000000, 0, [0x9F, 0, 0], (0x0B01, 1, 0x08)),
            (0, 1000000, 0, jedec, (0x0B02, 1, 0x08)),
            (0, 1000000, 0, jedec, (0x0B01, 2, 0x08)),
            (0, 1000000, 0, jedec, (0x0B01, 1, 0x18)),
            (0, 250000, 1, [0x05, 0x00], (0x0203, 1, 0x08)),
            (0, 500000, 2, [0x00] * 4, (0x0202, 1, 0x18)),
            (0, 2000000, 0, [0xFF] * 32, (0xFFFE, 255, 0x18)),
            (0, 1000000, 0, [0x5A], (0x0001, 0, 0x08))):
        rows.append((slot, hz, mode, tx, ident[0], ident[1], ident[2],
                     ref_fp_spi(slot, hz, mode, tx, *ident)))
    return rows


def spi_dev_vectors():
    out = []
    txs = [[0x9F, 0, 0, 0], [0x9F], [0x9F, 0], [0x9F] + [0] * 31, [0x05, 0], [0x05],
           [0x03, 0x00, 0x00, 0x00, 0, 0, 0, 0], [0x03, 0x12, 0x34, 0xFE, 0, 0, 0, 0, 0],
           [0x03, 0xFF, 0xFF, 0xFD, 0, 0, 0, 0, 0], [0x03, 0, 0, 0], [0x03], [0x06],
           [0xC7, 0, 0], [0x02, 0, 0, 0, 0xAA], [0xAA, 0x55, 0xA5, 0x5A], [0x01],
           list(range(32)), [0xDE, 0xAD, 0xBE, 0xEF]]
    for tx in txs:
        out.append((SPI_DEV_FLASH, tx, ref_spi_flash(tx)))
        out.append((SPI_DEV_ECHO, tx, ref_spi_echo(tx)))
        out.append((SPI_DEV_NONE, tx, [0] * len(tx)))
    return out


def render_spi():
    L = [
        "/* Generated by scripts/gen_lab_vectors.py — do not edit.",
        " * SPEC-DERIVED, NOT A RECORDING OF A REAL DEVICE (ADR-0023 §6): which slot may be probed and",
        " * why not, the request's fingerprint (FNV-1a) and what the test-only SPI devices answer,",
        " * each with the result an independent Python implementation computes.",
        " * Test infrastructure only. */",
        "/* clang-format off */",
        "#pragma once",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "/* state: 0 ABSENT, 1 KNOWN, 2 GENERIC, 3 CONFLICT (stark_module.h) */",
        "typedef struct {",
        "    uint8_t cs;",
        "    uint8_t state;",
        "    uint8_t unsupported;",
        "    uint32_t declared;",
        "    uint8_t eligible;",
        "    const char *why;",
        "} fake_lab_spi_judge_vec_t;",
        "",
        "typedef struct {",
        "    uint8_t slot;",
        "    uint32_t hz;",
        "    uint8_t mode;",
        "    const uint8_t *tx;",
        "    uint8_t n;",
        "    uint16_t id;",
        "    uint8_t rev;",
        "    uint32_t declared;",
        "    uint32_t fp;",
        "} fake_lab_spi_fp_vec_t;",
        "",
        "/* device: 0 none, 1 flash, 2 echo (fake_lab_spi_mode_t) */",
        "typedef struct {",
        "    uint8_t device;",
        "    const uint8_t *tx;",
        "    const uint8_t *rx;",
        "    uint8_t n;",
        "} fake_lab_spi_dev_vec_t;",
        "",
    ]

    def arr(name, vals):
        L.append("static const uint8_t %s[] = {%s};" % (name, ",".join("0x%02x" % v for v in vals)))

    L.append("static const fake_lab_spi_judge_vec_t fake_lab_spi_judge_vecs[] = {")
    for cs, state, declared, unsupported, ok, why in spi_judge_vectors():
        L.append('    {%d, %d, %d, 0x%08xu, %d, "%s"},' % (int(cs), state, int(unsupported),
                                                          declared, int(ok), why))
    L.append("};")
    L.append("#define FAKE_LAB_SPI_JUDGE_VECS (sizeof(fake_lab_spi_judge_vecs) / "
             "sizeof(fake_lab_spi_judge_vecs[0]))")
    L.append("")

    fps = spi_fp_vectors()
    for i, (slot, hz, mode, tx, mid, rev, declared, fp) in enumerate(fps):
        arr("fake_lab_spi_fptx_%d" % i, tx)
    L.append("")
    L.append("static const fake_lab_spi_fp_vec_t fake_lab_spi_fp_vecs[] = {")
    for i, (slot, hz, mode, tx, mid, rev, declared, fp) in enumerate(fps):
        L.append("    {%d, %du, %d, fake_lab_spi_fptx_%d, %d, 0x%04x, %d, 0x%08xu, %du}," % (
            slot, hz, mode, i, len(tx), mid, rev, declared, fp))
    L.append("};")
    L.append("#define FAKE_LAB_SPI_FP_VECS (sizeof(fake_lab_spi_fp_vecs) / "
             "sizeof(fake_lab_spi_fp_vecs[0]))")
    L.append("")

    devs = spi_dev_vectors()
    for i, (dev, tx, rx) in enumerate(devs):
        arr("fake_lab_spi_dtx_%d" % i, tx)
        arr("fake_lab_spi_drx_%d" % i, rx)
    L.append("")
    L.append("static const fake_lab_spi_dev_vec_t fake_lab_spi_dev_vecs[] = {")
    for i, (dev, tx, rx) in enumerate(devs):
        L.append("    {%d, fake_lab_spi_dtx_%d, fake_lab_spi_drx_%d, %d}," % (dev, i, i, len(tx)))
    L.append("};")
    L.append("#define FAKE_LAB_SPI_DEV_VECS (sizeof(fake_lab_spi_dev_vecs) / "
             "sizeof(fake_lab_spi_dev_vecs[0]))")
    L.append("")
    return "\n".join(L)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args(argv)
    outputs = ((OUT, render()), (OUT_I2C, render_i2c()), (OUT_UART, render_uart()),
               (OUT_SPI, render_spi()))
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
