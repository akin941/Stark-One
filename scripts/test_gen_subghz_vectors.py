#!/usr/bin/env python3
"""Unit tests for scripts/gen_subghz_vectors.py (TASKS.md STARK-0603)."""
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
        real = gen.OUT
        try:
            with tempfile.TemporaryDirectory() as d:
                gen.OUT = Path(d) / "scenes.h"
                self.assertEqual(gen.main(["--check"]), 1)      # missing
                self.assertEqual(gen.main([]), 0)               # written
                self.assertEqual(gen.main(["--check"]), 0)      # current
                gen.OUT.write_text("stale")
                self.assertEqual(gen.main(["--check"]), 1)
        finally:
            gen.OUT = real

    def test_the_committed_header_is_current(self):
        self.assertEqual(gen.main(["--check"]), 0)


if __name__ == "__main__":
    unittest.main()
