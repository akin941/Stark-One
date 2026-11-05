#!/usr/bin/env python3
"""Unit tests for scripts/gen_module_vectors.py (TASKS.md STARK-0501)."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_module_vectors as gen  # noqa: E402


class Encoder(unittest.TestCase):
    def test_crc_check_value(self):
        # The catalogue's standard check value for CRC-16/CCITT-FALSE.
        self.assertEqual(gen.crc16(b"123456789"), 0x29B1)

    def test_layout(self):
        img = gen.encode(0x1234, 7, 0x04030201, "Name")
        self.assertEqual(len(img), 32)
        self.assertEqual(img[:4], b"SM\x01\x00")
        self.assertEqual(img[4:6], b"\x34\x12")
        self.assertEqual(img[6], 7)
        self.assertEqual(img[8:12], b"\x01\x02\x03\x04")
        self.assertEqual(img[12:30], b"Name".ljust(18, b"\0"))

    def test_every_image_is_32_bytes_and_labels_unique(self):
        es = gen.entries()
        self.assertTrue(all(len(e[1]) == 32 for e in es))
        self.assertEqual(len({e[0] for e in es}), len(es))

    def test_committed_file_is_current(self):
        self.assertEqual(gen.main(["--check"]), 0)


if __name__ == "__main__":
    unittest.main()
