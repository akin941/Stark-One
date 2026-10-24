#!/usr/bin/env python3
"""Unit tests for scripts/gen_ir_vectors.py (TASKS.md STARK-0301)."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_ir_vectors as gen  # noqa: E402


class Encoders(unittest.TestCase):
    def test_nec_shape(self):
        t = gen.nec(0x10, 0x12)
        self.assertEqual(len(t), 67)
        self.assertEqual(t[:2], [9000, 4500])
        self.assertEqual(t[-1], 560)
        # address LSB first: 0x10 -> bit 4 is the fifth data bit (a one space)
        self.assertEqual(t[2 + 2 * 4 + 1], 1690)
        self.assertEqual(gen.nec_repeat(), [9000, 2250, 560])


class Vectors(unittest.TestCase):
    def test_each_protocol_has_twenty(self):
        for name, fn in gen.PROTOCOLS.items():
            self.assertGreaterEqual(len(fn()), 20, name)

    def test_deterministic(self):
        for name, fn in gen.PROTOCOLS.items():
            self.assertEqual(gen.render(name, fn()), gen.render(name, fn()), name)

    def test_labelled_spec_derived(self):
        self.assertIn("SPEC-DERIVED, NOT RECORDED", gen.render("nec", gen.nec_vectors()))

    def test_committed_files_are_current(self):
        self.assertEqual(gen.main(["--check"]), 0)


if __name__ == "__main__":
    unittest.main()
