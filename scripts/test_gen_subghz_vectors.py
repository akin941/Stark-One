#!/usr/bin/env python3
"""Unit tests for scripts/gen_subghz_vectors.py (TASKS.md STARK-0603, STARK-0604)."""
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_subghz_vectors as gen  # noqa: E402


class Hash(unittest.TestCase):
    def test_fnv1a32_published_vectors(self):
        self.assertEqual(gen.fnv1a32(b""), 0x811C9DC5)
        self.assertEqual(gen.fnv1a32(b"a"), 0xE40C292C)
        self.assertEqual(gen.fnv1a32(b"foobar"), 0xBF9CF968)

    def test_symbols_hash_as_little_endian_int32(self):
        self.assertEqual(gen.symbol_hash([1, -2]),
                         gen.fnv1a32(bytes([1, 0, 0, 0, 0xFE, 0xFF, 0xFF, 0xFF])))


class Capture(unittest.TestCase):
    def test_leading_silence_dropped_and_same_sign_merged(self):
        self.assertEqual(gen.capture([-500, -500, 400, 100, -300, 0, -200, 50]),
                         [500, -500, 50])

    def test_symbol_limit(self):
        stream = [10 if i % 2 == 0 else -10 for i in range(3000)]
        kept = gen.capture(stream)
        self.assertEqual(len(kept), 2048)
        self.assertEqual(kept, stream[:2048])

    def test_two_second_limit_cuts_the_last_symbol(self):
        kept = gen.capture([900_000, -900_000, 900_000, -900_000])
        self.assertEqual(kept, [900_000, -900_000, 200_000])
        self.assertEqual(sum(abs(v) for v in kept), gen.MAX_TOTAL_US)

    def test_a_symbol_is_at_most_a_second(self):
        self.assertEqual(gen.capture([1_500_000, -1_500_000]), [1_000_000, -1_000_000])
        self.assertEqual(gen.capture([700_000, 700_000]), [1_000_000])

    def test_nothing_but_silence_keeps_nothing(self):
        self.assertEqual(gen.capture([-100, -200]), [])


class Histogram(unittest.TestCase):
    def test_two_clusters(self):
        h = gen.histogram([400, -1200, 410, -1190, 390, -1210])
        self.assertEqual(h, [(3, 400), (3, 1200)])

    def test_grouping_window_is_one_and_a_half_times_the_shortest(self):
        self.assertEqual(gen.histogram([100, 150, 151]), [(2, 125), (1, 151)])

    def test_at_most_eight_bins(self):
        h = gen.histogram([2 ** i for i in range(12)])
        self.assertEqual(len(h), 8)

    def test_mean_rounds_to_nearest(self):
        self.assertEqual(gen.histogram([100, 101]), [(2, 101)])  # 100.5 rounds up


class Scenes(unittest.TestCase):
    def setUp(self):
        self.scenes = {name: syms for name, _, syms in gen.scenes()}

    def test_every_scene_alternates_in_sign(self):
        for name, syms in self.scenes.items():
            for a, b in zip(syms, syms[1:]):
                self.assertTrue((a > 0) != (b > 0), name)

    def test_pwm_bit_mapping(self):
        syms = gen.pwm(0b10, 2, repeats=1)
        self.assertEqual(syms, [1200, -400, 400, -(1200 + gen.GAP_US)])

    def test_ppm_has_a_silence_first_and_equal_pulses(self):
        syms = self.scenes["ppm24"]
        self.assertLess(syms[0], 0)
        self.assertEqual({v for v in syms if v > 0}, {400})
        self.assertEqual(len(gen.capture(syms)), len(syms) - 1)

    def test_manchester_has_only_t_and_2t(self):
        syms = self.scenes["manchester32"]
        # the last bit's closing half joins the 12 ms gap that ends each repeat
        self.assertEqual({abs(v) for v in syms} - {500 + gen.GAP_US}, {500, 1000})

    def test_limit_scenes_exceed_the_limits(self):
        self.assertGreater(len(self.scenes["dense"]), gen.MAX_SYMBOLS)
        self.assertGreater(sum(abs(v) for v in self.scenes["slow"]), gen.MAX_TOTAL_US)
        self.assertEqual(len(gen.capture(self.scenes["dense"])), gen.MAX_SYMBOLS)
        self.assertEqual(len(gen.capture(self.scenes["slow"])), 7)

    def test_pwm_scene_histogram(self):
        self.assertEqual(gen.histogram(gen.capture(self.scenes["pwm24"]))[0][1], 400)

    def test_numbering_is_stable(self):
        self.assertEqual([n for n, _, _ in gen.scenes()],
                         ["pwm24", "ppm24", "manchester32", "dense", "slow"])


class Output(unittest.TestCase):
    def test_render_is_deterministic_and_declares_every_scene(self):
        text = gen.render()
        self.assertEqual(text, gen.render())
        self.assertIn("#define FAKE_SUBGHZ_SCENE_COUNT 5", text)
        self.assertIn("FAKE_SUBGHZ_SCENE_PWM24 1", text)

    def test_check_flags_a_stale_file(self):
        real = gen.OUT, gen.OUT_DEC
        try:
            with tempfile.TemporaryDirectory() as d:
                gen.OUT = Path(d) / "scenes.h"
                gen.OUT_DEC = Path(d) / "decode.h"
                self.assertEqual(gen.main(["--check"]), 1)      # missing
                self.assertEqual(gen.main([]), 0)               # written
                self.assertEqual(gen.main(["--check"]), 0)      # current
                gen.OUT.write_text("stale")
                self.assertEqual(gen.main(["--check"]), 1)
                self.assertEqual(gen.main([]), 0)
                gen.OUT_DEC.write_text("stale")                 # either file
                self.assertEqual(gen.main(["--check"]), 1)
        finally:
            gen.OUT, gen.OUT_DEC = real

    def test_the_committed_header_is_current(self):
        self.assertEqual(gen.main(["--check"]), 0)


class Reference(unittest.TestCase):
    """The independent reference decoder of STARK-0604 (docs/subghz-decode.md)."""

    def test_the_three_spec_scenes(self):
        pwm, ppm, man, dense, slow = gen.scene_expectations()
        self.assertEqual(pwm["bursts"], [{"kind": "pwm", "bits": 24, "hex": "a5c3e1", "repeats": 4,
                                          "short": 400, "long": 1200}])
        self.assertEqual(ppm["bursts"], [{"kind": "ppm", "bits": 24, "hex": "5a3cc3", "repeats": 4,
                                          "short": 400, "long": 1200}])
        self.assertEqual(man["bursts"], [{"kind": "manchester", "bits": 32, "hex": "c3a5f00f",
                                          "repeats": 3, "short": 500, "long": 1000}])
        self.assertEqual((dense["decoded"], dense["unknown"]), (0, 1))
        self.assertEqual((slow["decoded"], slow["unknown"]), (0, 1))

    def test_helpers(self):
        self.assertEqual(gen.mean_round([1, 2]), 2)           # halves round up
        self.assertEqual(gen.mean_round([400]), 400)
        self.assertTrue(gen.near(500, 400))                   # exactly 25 %
        self.assertFalse(gen.near(501, 400))
        self.assertTrue(gen.near(300, 400))
        self.assertFalse(gen.near(299, 400))
        self.assertEqual(gen.bits_hex([1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 0, 0]), "a5c0")
        self.assertEqual(gen.bits_hex([]), "")

    def test_two_means(self):
        self.assertIsNone(gen.two_means([400]))
        self.assertIsNone(gen.two_means([400, 400, 400]))
        self.assertIsNone(gen.two_means([400, 440, 400, 440]))   # one spread, not two clusters
        self.assertEqual(gen.two_means([400, 410, 1200, 1210])[:2], (405, 1205))

    def test_a_capture_must_be_one(self):
        for bad in ([0], [-5, 5], [5, 5], [1_000_001], [5, -5, -5]):
            with self.assertRaises(ValueError):
                gen.ref_decode(bad)
        with self.assertRaises(ValueError):
            gen.ref_decode([10 if i % 2 == 0 else -10 for i in range(2049)])
        self.assertEqual(gen.ref_decode([])["decoded"], 0)

    def test_ambiguity_is_unknown(self):
        sym = gen.frames(gen.h_ppm(0x020, 12, 500, 500, 1000), 2)
        self.assertEqual(len(gen.ref_kinds([abs(v) for v in sym[:25]])), 2)
        self.assertEqual(gen.ref_decode(sym)["decoded"], 0)

    def test_manchester_alignment(self):
        for first in (0, 1):
            for last in (0, 1):
                value = (first << 15) | 0x1234 << 1 & 0x7FFE | last
                sym = gen.frames(gen.h_man(value, 16, 500), 2)
                r = gen.ref_decode(sym)
                self.assertEqual(r["bursts"][0]["hex"], "%04x" % value)
                self.assertEqual(r["bursts"][0]["repeats"], 2)

    def test_more_than_eight_distinct_bursts(self):
        seq = []
        for k in range(10):
            seq += gen.h_pwm(0x100 + k * 37, 16, 400, 1200) + [(False, gen.GAP_US)]
        r = gen.ref_decode(gen.capture(gen.runs(seq)))
        self.assertEqual((r["decoded"], len(r["bursts"]), r["omitted"]), (10, 8, 2))


class Rng(unittest.TestCase):
    def test_stream_is_fixed(self):
        a, b = gen.Rng(1), gen.Rng(1)
        self.assertEqual([a.next() for _ in range(4)], [b.next() for _ in range(4)])
        self.assertEqual([gen.Rng(7).below(100) for _ in range(2)][0], gen.Rng(7).below(100))
        r = gen.Rng(0)                                         # a zero seed still runs
        self.assertNotEqual(r.next(), r.next())

    def test_helpers_stay_in_range(self):
        r = gen.Rng(5)
        for _ in range(200):
            self.assertTrue(3 <= r.between(3, 9) <= 9)
            self.assertTrue(0 <= r.bits(12) < 4096)

    def test_jitter_is_bounded_and_keeps_signs(self):
        sym = [400, -1200, 10, -1] * 25
        out = gen.jitter(sym, gen.Rng(9), 10)
        for v, w in zip(sym, out):
            self.assertEqual(v > 0, w > 0)
            self.assertTrue(abs(abs(w) - abs(v)) <= max(1, abs(v) // 10 + 1))
            self.assertNotEqual(w, 0)


class DecodeVectors(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.vecs = gen.decode_vectors()

    def test_deterministic(self):
        self.assertEqual(gen.render_decode(), gen.render_decode())

    def test_twenty_jittered_vectors_per_kind(self):
        per = {}
        for v in self.vecs:
            if v["jit"] == 10:
                k = v["expect"]["bursts"][0]["kind"]
                per[k] = per.get(k, 0) + 1
        self.assertTrue(all(per.get(k, 0) >= 20 for k in ("pwm", "ppm", "manchester")), per)

    def test_every_vector_is_a_capture_the_reference_accepts(self):
        for v in self.vecs:
            self.assertEqual(gen.ref_decode(v["sym"]), v["ref"], v["name"])
            self.assertEqual(v["expect"], v["ref"], v["name"])
            self.assertTrue(len(v["sym"]) <= gen.MAX_SYMBOLS, v["name"])

    def test_edges_come_in_pairs_either_side_of_the_tolerance(self):
        edges = [v for v in self.vecs if v["name"].startswith("edge_")]
        self.assertEqual(len(edges), 28)
        for v in edges:
            if v["name"].endswith("_in"):
                self.assertEqual(v["ref"]["unknown"], 0, v["name"])
            else:
                self.assertEqual(v["ref"]["unknown"], 1, v["name"])
                self.assertEqual(v["ref"]["decoded"], 2, v["name"])

    def test_noise_never_decodes(self):
        noise = [v for v in self.vecs if v["name"].startswith("noise_")]
        self.assertTrue(len(noise) >= 16)
        self.assertTrue(all(v["ref"]["decoded"] == 0 for v in noise))

    def test_limits_of_bit_count(self):
        by = {v["name"]: v for v in self.vecs}
        for kind in ("pwm", "ppm", "manchester"):
            self.assertEqual(by["limit_%s_8" % kind]["ref"]["decoded"], 1)
            self.assertEqual(by["limit_%s_128" % kind]["ref"]["bursts"][0]["bits"], 128)
            self.assertEqual(by["limit_%s_7" % kind]["ref"]["decoded"], 0)
            self.assertEqual(by["limit_%s_129" % kind]["ref"]["decoded"], 0)

    def test_render_declares_the_tables(self):
        text = gen.render_decode()
        self.assertIn("#define FAKE_DEC_VECTOR_COUNT %d" % len(self.vecs), text)
        self.assertIn("k_fake_dec_vectors", text)
        self.assertIn("k_fake_dec_scenes", text)
        self.assertIn('"5a3cc3"', text)


if __name__ == "__main__":
    unittest.main()
