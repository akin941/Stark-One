#!/usr/bin/env python3
"""Unit tests for scripts/gen_nfc_vectors.py (TASKS.md STARK-0401)."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_nfc_vectors as gen  # noqa: E402


class Encoder(unittest.TestCase):
    def test_short_uri_record(self):
        # NFC Forum URI RTD example: "https://example.com" -> prefix 0x04
        msg = gen.message([gen.uri_rec("https://example.com")])
        self.assertEqual(msg[:4], bytes([0xD1, 0x01, 0x0C, ord("U")]))
        self.assertEqual(msg[4], 0x04)
        self.assertEqual(msg[5:], b"example.com")

    def test_long_record_and_flags(self):
        rec = gen.record(gen.TNF_MIME, b"a/b", b"id", bytes(300), mb=True, me=True)
        self.assertEqual(rec[0], 0xC0 | 0x08 | gen.TNF_MIME)  # MB ME IL, no SR
        self.assertEqual(rec[2:6], (300).to_bytes(4, "big"))

    def test_mb_me_on_ends_only(self):
        msg = gen.message([gen.text_rec("en", "a"), gen.text_rec("en", "b"),
                           gen.text_rec("en", "c")])
        hdrs = [msg[0], msg[8], msg[16]]
        self.assertEqual([h & 0xC0 for h in hdrs], [0x80, 0x00, 0x40])

    def test_text_payload(self):
        self.assertEqual(gen.text_payload("en", "hi"), b"\x02enhi")

    def test_prefix_table_size(self):
        self.assertEqual(len(gen.URI_PREFIXES), 0x24)


class Vectors(unittest.TestCase):
    def test_counts(self):
        v = gen.ndef_vectors()
        self.assertGreaterEqual(sum(1 for x in v if x["expect"] == "STARK_OK"), 40)
        self.assertGreaterEqual(sum(1 for x in v if x["expect"] != "STARK_OK"), 20)

    def test_deterministic(self):
        for name, fn in gen.OUTPUTS.items():
            self.assertEqual(fn(), fn(), name)

    def test_committed_headers_current(self):
        self.assertEqual(gen.main(["--check"]), 0)


if __name__ == "__main__":
    unittest.main()
