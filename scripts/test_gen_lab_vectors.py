#!/usr/bin/env python3
"""Unit tests for scripts/gen_lab_vectors.py (TASKS.md STARK-0703, 0704, 0705, 0706)."""
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
            old = (gen.OUT, gen.OUT_I2C, gen.OUT_UART, gen.OUT_SPI)
            try:
                gen.OUT = Path(d) / "fake_lab_vectors.h"
                gen.OUT_I2C = Path(d) / "fake_lab_i2c_vectors.h"
                gen.OUT_UART = Path(d) / "fake_lab_uart_vectors.h"
                gen.OUT_SPI = Path(d) / "fake_lab_spi_vectors.h"
                self.assertEqual(gen.main(["--check"]), 1)  # missing
                self.assertEqual(gen.main([]), 0)
                self.assertEqual(gen.main(["--check"]), 0)
                gen.OUT.write_text("stale")
                self.assertEqual(gen.main(["--check"]), 1)
                self.assertEqual(gen.main([]), 0)
                gen.OUT_I2C.write_text("stale")  # any file being stale fails the check
                self.assertEqual(gen.main(["--check"]), 1)
                self.assertEqual(gen.main([]), 0)
                gen.OUT_UART.write_text("stale")
                self.assertEqual(gen.main(["--check"]), 1)
                self.assertEqual(gen.main([]), 0)
                gen.OUT_SPI.write_text("stale")
                self.assertEqual(gen.main(["--check"]), 1)
            finally:
                gen.OUT, gen.OUT_I2C, gen.OUT_UART, gen.OUT_SPI = old

    def test_the_committed_header_is_current(self):
        self.assertEqual(gen.main(["--check"]), 0)

    def test_the_header_declares_what_the_tests_use(self):
        text = gen.render()
        for name in ("fake_lab_vec_t", "fake_lab_vecs", "FAKE_LAB_VECS", "fake_lab_plan_vecs",
                     "FAKE_LAB_PLAN_VECS"):
            self.assertIn(name, text)
        self.assertIn("SPEC-DERIVED, NOT A RECORDING", text)


class UartReference(unittest.TestCase):
    """STARK-0705: the independent restatement of the UART tool's rules."""

    def test_wire_time_rounds_up_and_the_window_is_twice_it_plus_20_ms(self):
        self.assertEqual(gen.uart_bits(0), 10)
        self.assertEqual(gen.uart_bits(1), 11)
        self.assertEqual(gen.uart_bits(3), 11)
        self.assertEqual(gen.uart_bits(4), 10)
        self.assertEqual(gen.ref_wire_ms(64, 115200, 0), 6)  # 5.56 ms
        self.assertEqual(gen.ref_wire_ms(64, 1200, 3), 587)  # 586.7 ms
        self.assertEqual(gen.ref_window_ms(115200, 0), 32)
        self.assertEqual(gen.ref_window_ms(1200, 3), 1194)

    def test_the_ring_keeps_the_newest_bytes_and_counts_the_rest(self):
        self.assertEqual(gen.ref_ring(8, 5, 1), (5, 0, [0, 1, 2, 3, 4]))
        count, lost, kept = gen.ref_ring(8, 20, 3)
        self.assertEqual((count, lost, kept), (8, 12, list(range(12, 20))))
        count, lost, kept = gen.ref_ring(4096, 9000, 64)
        self.assertEqual((count, lost), (4096, 9000 - 4096))
        self.assertEqual(kept[0], 4904 & 0xFF)

    def test_loopback_judgement(self):
        p = list(range(64))
        self.assertEqual(gen.ref_loop(p, 64, 0, 0, 0), (gen.LOOP_OK, 64, 0, -1))
        self.assertEqual(gen.ref_loop([], 64, 0, 0, 0), (gen.LOOP_NO_DATA, 0, 64, 0))
        self.assertEqual(gen.ref_loop([], 64, 3, 0, 1), (gen.LOOP_NO_DATA, 0, 68, 0))
        self.assertEqual(gen.ref_loop(p, 64, 2, 0, 0), (gen.LOOP_LINE_ERRORS, 64, 2, -1))
        flipped = p[:5] + [p[5] ^ 1] + p[6:]
        self.assertEqual(gen.ref_loop(flipped, 64, 0, 0, 0), (gen.LOOP_MISMATCH, 64, 1, 5))
        self.assertEqual(gen.ref_loop(p[:40], 64, 0, 0, 0), (gen.LOOP_MISMATCH, 40, 24, 40))
        self.assertEqual(gen.ref_loop(p + [9], 64, 0, 0, 0), (gen.LOOP_MISMATCH, 65, 1, 64))
        # a mismatch is never a success, whatever else is true
        for v in gen.loop_vectors():
            name, rx, sent, f, pa, o, status, received, errors, first = v
            if status == gen.LOOP_OK:
                self.assertEqual((errors, first), (0, -1), name)

    def test_rows(self):
        self.assertEqual(gen.ref_hex_row([0, 0x41, 0xFF], 64), "00 41 ff")
        self.assertEqual(gen.ref_hex_row([0, 0x41, 0xFF], 7), "00 41")  # "00 41 ff" needs 9
        self.assertEqual(gen.ref_hex_row([1], 0), "")
        self.assertEqual(gen.ref_ascii_row([0x41, 0x7F, 0x20, 0x1F], 64), "A. .")
        self.assertEqual(gen.ref_ascii_row([0x41, 0x42], 2), "A")

    def test_the_send_file(self):
        P = gen.ref_parse_send
        self.assertEqual(P(b"AT\r\n"), (gen.FILE_OK, [0x41, 0x54]))
        self.assertEqual(P(b"hex:41 54 0d 0a"), (gen.FILE_OK, [0x41, 0x54, 0x0D, 0x0A]))
        self.assertEqual(P(b"hex:4a4B"), (gen.FILE_OK, [0x4A, 0x4B]))
        self.assertEqual(P(b""), (gen.FILE_EMPTY, []))
        self.assertEqual(P(b"hex:"), (gen.FILE_EMPTY, []))
        self.assertEqual(P(b"x" * 33)[0], gen.FILE_TOO_BIG)
        self.assertEqual(P(b"x" * 1025)[0], gen.FILE_TOO_BIG)
        self.assertEqual(P(b"a\tb")[0], gen.FILE_BAD)
        self.assertEqual(P(b"hex:4")[0], gen.FILE_BAD)
        self.assertEqual(P(b"hex:0x41")[0], gen.FILE_BAD)
        self.assertEqual(P(b"hexagon"), (gen.FILE_OK, list(b"hexagon")))  # only `hex:` is hex
        self.assertEqual(P(b"hex:" + b"41 " * 33)[0], gen.FILE_TOO_BIG)

    def test_log_names_and_crc(self):
        self.assertEqual(gen.ref_log_name("uart_0001.log"), (True, 1))
        self.assertEqual(gen.ref_log_name("uart_9999.log"), (True, 9999))
        self.assertEqual(gen.ref_log_name("uart_001.log"), (False, 0))
        self.assertEqual(gen.ref_log_name("uart_000a.log"), (False, 0))
        parse, nxt = gen.name_vectors()
        self.assertEqual([n for n in nxt if n[1]], [(9999, True, "")])
        self.assertEqual(gen.zlib.crc32(b"123456789"), 0xCBF43926)

    def test_the_scenes_are_what_the_fake_peer_says_and_the_log_scenario_names_their_crc(self):
        text = gen.scene_text()
        self.assertEqual(len(text), 1300)
        self.assertTrue(text.startswith(b"stark uart scene line 00\r\n"))
        self.assertTrue(text.endswith(b"stark uart scene line 49\r\n"))
        self.assertEqual(len(gen.scene_binary()), 1024)
        scenario = (gen.ROOT / "test/emu/storage/v07-uart-log.toml").read_text()
        self.assertIn("crc 0x%08x" % gen.zlib.crc32(text), scenario)
        self.assertIn("crc 0x%08x" % gen.zlib.crc32(gen.scene_binary()), scenario)

    def test_a_frame_is_start_data_lsb_first_parity_stop(self):
        self.assertEqual(gen.frame_bits(0x41, 0), [0, 1, 0, 0, 0, 0, 0, 1, 0, 1])  # 'A' 8N1
        self.assertEqual(gen.frame_bits(0x03, 1), [0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1])  # 8E1: two ones
        self.assertEqual(gen.frame_bits(0x01, 1)[9], 1)  # an odd count gets an even parity bit of 1
        self.assertEqual(gen.frame_bits(0x01, 2)[9], 0)  # and an odd-parity bit of 0
        self.assertEqual(gen.frame_bits(0x00, 3), [0] + [0] * 8 + [1, 1])  # 8N2
        self.assertEqual(len(gen.frame_bits(0x7F, 4)), 10)  # 7E1

    def test_edges_are_ordered_and_spaced_by_the_bit_time(self):
        ts = gen.uart_edges(bytes([0x55] * 4), 115200, 0, 1000)
        self.assertEqual(ts[0], 1000)
        self.assertTrue(all(b > a for a, b in zip(ts, ts[1:])))
        self.assertTrue(all(abs((b - a) - gen.TICK_HZ // 115200) <= 1 for a, b in zip(ts, ts[1:])))
        wrapped = gen.uart_edges(bytes([0x55] * 4), 115200, 0, 0xFFFFFF00)
        self.assertTrue(all(0 <= t <= 0xFFFFFFFF for t in wrapped))

    def test_every_listed_baud_is_named_and_every_status_appears(self):
        vs = gen.autobaud_vectors()
        named = {v[3] for v in vs if v[3]}
        self.assertEqual(named, set(gen.UART_BAUDS))
        self.assertEqual({v[4] for v in vs}, set(range(5)))
        names = [v[0] for v in vs]
        self.assertEqual(len(names), len(set(names)))
        self.assertGreaterEqual(len(vs), 60)

    def test_the_snap_is_3_percent(self):
        for baud in (9600, 115200):
            for pct, ok in ((-2.9, True), (2.9, True), (-3.1, False), (3.1, False)):
                period = gen.TICK_HZ / (baud * (1 + pct / 100))
                ts = [int(i * period) for i in range(40)]
                status = gen.ref_autobaud(ts, gen.TICK_HZ)[0]
                self.assertEqual(status == gen.BAUD_OK, ok, (baud, pct))

    def test_hostile_input(self):
        A = gen.ref_autobaud
        T = gen.TICK_HZ
        self.assertEqual(A([], T)[0], gen.BAUD_TOO_FEW)
        self.assertEqual(A(list(range(15)), T)[0], gen.BAUD_TOO_FEW)
        self.assertEqual(A([5000] * 20, T)[0], gen.BAUD_NO_PULSE)
        self.assertEqual(A([i * 2000 for i in range(20)], 0)[0], gen.BAUD_INVALID)
        self.assertEqual(A(list(range(0, 5000 * 10, 5000)) + [1] + list(range(0, 50000, 5000)),
                           T)[0], gen.BAUD_INVALID)
        self.assertEqual(A([0] * 2049, T)[0], gen.BAUD_INVALID)
        self.assertEqual(A([i * 2500 for i in range(20)], T)[0], gen.BAUD_UNKNOWN)  # 96 000


class SpiReference(unittest.TestCase):
    """STARK-0706: the independent restatement of the SPI probe's rules."""

    def test_only_a_known_wired_slot_with_spi_and_no_function_is_eligible(self):
        J = gen.ref_spi_judge
        K = gen.SPI_KNOWN
        self.assertEqual(J(True, K, gen.CAP_SPI, False), (True, ""))
        self.assertEqual(J(True, K, gen.CAP_SPI | gen.CAP_IRQ, False), (True, ""))
        self.assertEqual(J(True, K, gen.CAP_IRQ, False), (False, "no SPI declared"))
        self.assertEqual(J(True, K, 0, False), (False, "no SPI declared"))
        self.assertEqual(J(True, K, 0x38, False), (False, "owned by Sub-GHz"))
        self.assertEqual(J(True, K, gen.CAP_NFC | gen.CAP_IRQ, False), (False, "owned by NFC"))
        self.assertEqual(J(True, K, gen.CAP_IR_TX, False), (False, "owned by IR"))
        self.assertEqual(J(True, K, 0x80000008, False), (False, "owned by unknown"))
        self.assertEqual(J(True, K, 0x40 | gen.CAP_SPI, False), (False, "owned by unknown"))

    def test_the_reasons_come_in_the_order_they_are_decided(self):
        J = gen.ref_spi_judge
        self.assertEqual(J(False, gen.SPI_KNOWN, gen.CAP_SPI, False), (False, "no chip select"))
        self.assertEqual(J(True, gen.SPI_ABSENT, gen.CAP_SPI, False), (False, "no module"))
        self.assertEqual(J(True, gen.SPI_GENERIC, gen.CAP_SPI, False), (False, "generic module"))
        self.assertEqual(J(True, gen.SPI_GENERIC, 0, True), (False, "unsupported module"))
        self.assertEqual(J(True, gen.SPI_CONFLICT, gen.CAP_SPI, False), (False, "conflict"))

    def test_every_reason_and_both_outcomes_are_in_the_vectors(self):
        vecs = gen.spi_judge_vectors()
        self.assertGreater(len(vecs), 150)
        reasons = {v[5] for v in vecs}
        for why in ("", "no chip select", "no module", "generic module", "unsupported module",
                    "conflict", "owned by Sub-GHz", "owned by NFC", "owned by IR",
                    "owned by unknown", "no SPI declared"):
            self.assertIn(why, reasons)
        # nothing that declares a function bit is ever eligible
        for cs, state, declared, unsupported, ok, why in vecs:
            if declared & ~(gen.CAP_SPI | gen.CAP_IRQ) & 0xFFFFFFFF:
                self.assertFalse(ok)
            if ok:
                self.assertTrue(cs and state == gen.SPI_KNOWN and declared & gen.CAP_SPI)

    def test_the_fingerprint_changes_with_every_field(self):
        base = (0, 1000000, 0, [0x9F, 0, 0, 0], 0x0B01, 1, 0x08)
        seen = {gen.ref_fp_spi(*base)}
        for i, alt in enumerate((1, 2000000, 1, [0x9F, 0, 0, 1], 0x0B02, 2, 0x18)):
            args = list(base)
            args[i] = alt
            seen.add(gen.ref_fp_spi(*args))
        self.assertEqual(len(seen), 8)
        self.assertEqual(len({v[7] for v in gen.spi_fp_vectors()}), len(gen.spi_fp_vectors()))

    def test_the_flash(self):
        self.assertEqual(gen.ref_spi_flash([0x9F, 0, 0, 0]), [0, 0xEF, 0x40, 0x18])
        self.assertEqual(gen.ref_spi_flash([0x9F, 0]), [0, 0xEF])
        self.assertEqual(gen.ref_spi_flash([0x05, 0]), [0, 0])
        self.assertEqual(gen.ref_spi_flash([0x03, 0, 0, 0xFE, 0, 0, 0]), [0, 0, 0, 0, 0xFE, 0xFF, 0])
        self.assertEqual(gen.ref_spi_flash([0x03, 0xFF, 0xFF, 0xFF, 0, 0]), [0, 0, 0, 0, 0xFF, 0])
        self.assertEqual(gen.ref_spi_flash([0x03, 1, 2, 3]), [0, 0, 0, 0])
        self.assertEqual(gen.ref_spi_flash([0x06]), [0])

    def test_the_echo(self):
        self.assertEqual(gen.ref_spi_echo([1, 2, 3]), [0, 1, 2])
        self.assertEqual(gen.ref_spi_echo([7]), [0])

    def test_the_header_declares_what_the_tests_use(self):
        text = gen.render_spi()
        for name in ("fake_lab_spi_judge_vecs", "FAKE_LAB_SPI_JUDGE_VECS", "fake_lab_spi_fp_vecs",
                     "fake_lab_spi_dev_vecs", "FAKE_LAB_SPI_DEV_VECS"):
            self.assertIn(name, text)
        self.assertIn("SPEC-DERIVED, NOT A RECORDING", text)
        self.assertEqual(gen.render_spi(), gen.render_spi())


if __name__ == "__main__":
    unittest.main()
