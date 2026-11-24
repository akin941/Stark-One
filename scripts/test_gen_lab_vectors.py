#!/usr/bin/env python3
"""Unit tests for scripts/gen_lab_vectors.py (TASKS.md STARK-0703, STARK-0704)."""
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_lab_vectors as gen  # noqa: E402

T = gen.TICK_HZ


def wave(period, high, n, first=False, t0=1000):
    """n edges of a square wave of `period` ticks and `high` ticks high."""
    ts, t, level = [], t0, first
    for _ in range(n):
        ts.append(t)
        t += (period - high) if level else high
        level = not level
    return ts


class Reference(unittest.TestCase):
    def test_a_known_wave(self):
        st, hz, duty, period, _ = gen.ref_measure(wave(240000, 60000, 100), False, T, False)
        self.assertEqual((st, hz, duty, period), (gen.OK, 100000, 250, 240000))

    def test_the_first_edge_may_be_falling(self):
        st, hz, duty, _, _ = gen.ref_measure(wave(240000, 60000, 101, first=True), True, T, False)
        self.assertEqual((st, hz, duty), (gen.OK, 100000, 250))

    def test_statuses(self):
        self.assertEqual(gen.ref_measure([], True, T, False)[0], gen.NO_SIGNAL)
        self.assertTrue(gen.ref_measure([], True, T, False)[4])
        self.assertFalse(gen.ref_measure([], False, T, False)[4])
        self.assertEqual(gen.ref_measure([1, 2], False, T, True)[0], gen.OVERFLOW)
        self.assertEqual(gen.ref_measure([1, 2], False, 0, False)[0], gen.INVALID)
        self.assertEqual(gen.ref_measure([0] * (gen.RING + 1), False, T, False)[0], gen.INVALID)
        self.assertEqual(gen.ref_measure([100, 200, 300], False, T, False)[0], gen.GLITCH_S)
        self.assertEqual(gen.ref_measure([100, 5000, 2000], False, T, False)[0], gen.INVALID)
        self.assertEqual(gen.ref_measure([100, 120100], False, T, False)[0], gen.TOO_SLOW)
        # exactly 1 us (240 ticks) is not a glitch
        self.assertEqual(gen.ref_measure([100, 340, 580, 820, 1060], False, T, False)[0],
                         gen.TOO_FAST)

    def test_the_counter_is_unwrapped(self):
        ts = [(0xFFFFFFFF - 200000 + i * 120000) & 0xFFFFFFFF for i in range(10)]
        st, hz, duty, _, _ = gen.ref_measure(ts, False, T, False)
        self.assertEqual((st, hz, duty), (gen.OK, 100000, 500))

    def test_rounding_is_half_up(self):
        self.assertEqual(gen.round_half_up(gen.Fraction(5, 2)), 3)
        self.assertEqual(gen.round_half_up(gen.Fraction(7, 3)), 2)
        self.assertEqual(gen.round_half_up(gen.Fraction(0)), 0)


class Plan(unittest.TestCase):
    def test_the_rule(self):
        self.assertEqual(gen.ref_plan(0), (False, 4000))
        self.assertEqual(gen.ref_plan(1), (False, 4000))
        self.assertEqual(gen.ref_plan(2000), (False, 10))
        self.assertEqual(gen.ref_plan(2002), (False, 10))
        self.assertEqual(gen.ref_plan(2003), (True, 0))
        self.assertEqual(gen.ref_plan(40), (False, 384))
        self.assertEqual(gen.ref_plan(768), (False, 20))

    def test_the_window_never_overflows_the_ring(self):
        for c in range(1, 2003):
            _, ms = gen.ref_plan(c)
            self.assertLessEqual(c * ms // gen.GATE_MS, 1010)


class Signals(unittest.TestCase):
    def test_a_square_wave_has_the_edges_it_says(self):
        trans, level0 = gen.square_transitions(100000, 25, 5000, 5000 + T // 100)
        self.assertEqual(len(trans), 2 * 10)  # ten periods in 10 ms
        self.assertEqual([lvl for _, lvl in trans][:4], [True, False, True, False] if not level0
                         else [False, True, False, True])
        self.assertTrue(all(b[0] > a[0] for a, b in zip(trans, trans[1:])))

    def test_the_level_before_an_edge_at_the_start_is_its_opposite(self):
        trans, level0 = gen.square_transitions(100000, 50, 0, 1000000)
        self.assertEqual(trans[0], (0, True))
        self.assertFalse(level0)

    def test_jitter_is_bounded_and_keeps_order(self):
        trans, _ = gen.square_transitions(1000000, 50, 0, T // 50)
        moved = gen.jittered(trans, 480, gen.Rng(7))
        for (a, _), (b, _) in zip(trans, moved):
            self.assertLessEqual(abs(a - b), 480)
        self.assertTrue(all(y[0] > x[0] for x, y in zip(moved, moved[1:])))

    def test_the_rng_is_deterministic(self):
        a, b = gen.Rng(3), gen.Rng(3)
        self.assertEqual([a.next() for _ in range(5)], [b.next() for _ in range(5)])
        self.assertNotEqual(gen.Rng(3).next(), gen.Rng(4).next())

    def test_a_fast_gate_makes_no_capture(self):
        self.assertIsNone(gen.capture(8_000_000, 50, 1))


class Catalogue(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.vs = gen.vectors()

    def test_every_status_is_present(self):
        self.assertEqual({v["status"] for v in self.vs}, set(range(7)))

    def test_every_measured_vector_is_within_the_accuracy(self):
        measured = [v for v in self.vs if v["nominal_hz"]]
        self.assertGreaterEqual(len(measured), 45)
        for v in measured:
            err = abs(v["hz"] - v["nominal_hz"]) * 1_000_000 // v["nominal_hz"]
            self.assertLessEqual(err, v["tol_hz"], v["name"])
            self.assertLessEqual(abs(v["duty"] - v["nominal_duty"]), v["tol_duty"], v["name"])
            self.assertEqual(v["tol_hz"], 5000 if v["nominal_hz"] <= 1_000_000 else 20000)

    def test_every_stamp_fits_32_bits_and_the_ring(self):
        for v in self.vs:
            if v["name"] != "too_long":  # the one vector that is a ring too long on purpose
                self.assertLessEqual(len(v["ts"]), gen.RING)
            self.assertTrue(all(0 <= x <= 0xFFFFFFFF for x in v["ts"]))

    def test_names_are_unique(self):
        names = [v["name"] for v in self.vs]
        self.assertEqual(len(names), len(set(names)))

    def test_tolerances_are_the_adr_bands(self):
        self.assertEqual(gen.tolerances(100), (5000, 20))
        self.assertEqual(gen.tolerances(1_000_000), (5000, 20))
        self.assertEqual(gen.tolerances(1_000_001), (20000, 50))


class I2cReference(unittest.TestCase):
    """STARK-0704: the independent restatement of the scanner's rules."""

    def test_the_sweep(self):
        self.assertEqual(gen.ref_sweep(set(), set()), ([], 0, gen.SWEEP_OK, 112))
        self.assertEqual(gen.ref_sweep({0x08, 0x77}, set())[0], [0x08, 0x77])
        found, errors, status, probes = gen.ref_sweep({0x23}, {0x08, 0x09, 0x0A})
        self.assertEqual((found, errors, status, probes), ([], 3, gen.SWEEP_IO, 3))

    def test_two_errors_then_an_ack_or_a_nack_do_not_end_it(self):
        self.assertEqual(gen.ref_sweep({0x32}, {0x30, 0x31, 0x33, 0x34})[2], gen.SWEEP_OK)
        self.assertEqual(gen.ref_sweep(set(), {0x30, 0x31, 0x33, 0x34})[2], gen.SWEEP_OK)
        self.assertEqual(gen.ref_sweep(set(), {0x30, 0x31, 0x32})[2], gen.SWEEP_IO)

    def test_three_errors_on_the_last_addresses_end_it_after_every_probe(self):
        self.assertEqual(gen.ref_sweep({0x70}, {0x75, 0x76, 0x77})[1:], (3, gen.SWEEP_IO, 112))

    def test_fnv1a_known_values(self):
        self.assertEqual(gen.ref_fnv1a(b""), 2166136261)
        self.assertEqual(gen.ref_fnv1a(b"a"), 0xE40C292C)  # the published 32-bit FNV-1a of "a"
        self.assertEqual(gen.ref_fnv1a(b"foobar"), 0xBF9CF968)

    def test_a_fingerprint_changes_with_every_field(self):
        base = gen.ref_fp_write(0x68, 0x6B, False, [1])
        for other in (gen.ref_fp_write(0x69, 0x6B, False, [1]), gen.ref_fp_write(0x68, 0x6C, False, [1]),
                      gen.ref_fp_write(0x68, 0x6B, True, [1]), gen.ref_fp_write(0x68, 0x6B, False, [2]),
                      gen.ref_fp_write(0x68, 0x6B, False, [1, 0]), gen.ref_fp_dump(0x68)):
            self.assertNotEqual(base, other)

    def test_the_refusal_table(self):
        R = gen.ref_refusal
        self.assertEqual(R(0x50, 1, 0, False), gen.REFUSE_EEPROM)  # a write
        self.assertEqual(R(0x51, 2, 1, False), gen.REFUSE_EEPROM)  # more than a pointer before a read
        self.assertEqual(R(0x50, 1, 16, False), gen.REFUSE_NONE)  # a pointer and a read
        self.assertEqual(R(0x24, 1, 1, True), gen.REFUSE_NFC)
        self.assertEqual(R(0x24, 1, 1, False), gen.REFUSE_NONE)
        self.assertEqual(R(0x68, 10, 0, True), gen.REFUSE_NONE)

    def test_hex_text_keeps_whole_bytes_that_fit(self):
        self.assertEqual(gen.ref_hex([0xA5, 0x5A], 50, True), "a5 5a")
        self.assertEqual(gen.ref_hex([0xA5, 0x5A], 50, False), "a55a")
        self.assertEqual(gen.ref_hex([0xA5, 0x5A], 5, True), "a5")  # "a5 5a" needs 6
        self.assertEqual(gen.ref_hex([0xA5, 0x5A], 6, True), "a5 5a")
        self.assertEqual(gen.ref_hex([0xA5, 0x5A], 4, False), "a5")
        self.assertEqual(gen.ref_hex([0xA5], 2, False), "")
        self.assertEqual(gen.ref_hex([0xA5], 0, False), "")

    def test_vector_sets_are_large_and_named_once(self):
        names = [v[0] for v in gen.sweep_vectors()]
        self.assertEqual(len(names), len(set(names)))
        self.assertGreaterEqual(len(names), 15)
        self.assertGreaterEqual(len(gen.refusal_vectors()), 100)
        self.assertGreaterEqual(len(gen.hex_vectors()), 100)
        fps = [v[-1] for v in gen.fp_vectors()]
        self.assertEqual(len(fps), len(set(fps)))

    def test_the_i2c_header_declares_what_the_tests_use(self):
        text = gen.render_i2c()
        for name in ("fake_lab_sweep_vec_t", "fake_lab_sweep_vecs", "FAKE_LAB_SWEEP_VECS",
                     "fake_lab_fp_vecs", "fake_lab_refusal_vecs", "fake_lab_hex_vecs"):
            self.assertIn(name, text)
        self.assertIn("SPEC-DERIVED, NOT A RECORDING", text)
        self.assertEqual(gen.render_i2c(), gen.render_i2c())


class Output(unittest.TestCase):
    def test_render_is_deterministic(self):
        self.assertEqual(gen.render(), gen.render())

    def test_check_mode(self):
        with tempfile.TemporaryDirectory() as d:
            old, old_i2c = gen.OUT, gen.OUT_I2C
            try:
                gen.OUT = Path(d) / "fake_lab_vectors.h"
                gen.OUT_I2C = Path(d) / "fake_lab_i2c_vectors.h"
                self.assertEqual(gen.main(["--check"]), 1)  # missing
                self.assertEqual(gen.main([]), 0)
                self.assertEqual(gen.main(["--check"]), 0)
                gen.OUT.write_text("stale")
                self.assertEqual(gen.main(["--check"]), 1)
                self.assertEqual(gen.main([]), 0)
                gen.OUT_I2C.write_text("stale")  # either file being stale fails the check
                self.assertEqual(gen.main(["--check"]), 1)
            finally:
                gen.OUT, gen.OUT_I2C = old, old_i2c

    def test_the_committed_header_is_current(self):
        self.assertEqual(gen.main(["--check"]), 0)

    def test_the_header_declares_what_the_tests_use(self):
        text = gen.render()
        for name in ("fake_lab_vec_t", "fake_lab_vecs", "FAKE_LAB_VECS", "fake_lab_plan_vecs",
                     "FAKE_LAB_PLAN_VECS"):
            self.assertIn(name, text)
        self.assertIn("SPEC-DERIVED, NOT A RECORDING", text)


if __name__ == "__main__":
    unittest.main()
