#!/usr/bin/env python3
"""Unit tests for scripts/gen_module_vectors.py (TASKS.md STARK-0501)."""
import binascii
import os
import re
import shutil
import struct
import sys
import tempfile
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


class EepromImages(unittest.TestCase):
    """STARK-0803: the files an external programmer writes to a real 24C02."""

    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="stark-eeprom-")

    def tearDown(self):
        shutil.rmtree(self.dir)

    @staticmethod
    def committed_images():
        """The images in the committed header, which the firmware's parser is tested on
        (test_module_desc), read back from the C text rather than from entries()."""
        text = (gen.ROOT / "test/fake/fake_module_catalog.h").read_text()
        out = {}
        for m in re.finditer(r'\{"(\w+)",\s*\{([^}]*)\}', text):
            out[m.group(1)] = bytes(int(b, 16) for b in re.findall(r"0x([0-9A-F]{2})", m.group(2)))
        return out

    def test_every_entry_is_written_as_the_committed_descriptor_then_erased(self):
        paths = gen.emit(self.dir)
        committed = self.committed_images()
        es = gen.entries()
        self.assertEqual(len(paths), len(es))
        for i, (path, e) in enumerate(zip(paths, es)):
            self.assertEqual(path.name, "%02d_%s.bin" % (i + 1, e[0]))
            data = path.read_bytes()
            self.assertEqual(len(data), 256)
            self.assertEqual(data[:32], committed[e[0]], e[0])
            self.assertEqual(data[32:], b"\xFF" * 224, e[0])

    def test_the_bringup_modules_decode_independently(self):
        # Decoded with struct and binascii's CRC-CCITT, not with gen's encoder.
        want = {"cc1101_module": (0x0A01, 1, 0x38, b"CC1101 433 MHz"),
                "spi_breakout": (0x0B01, 1, 0x08, b"SPI breakout"),
                "nfc_carrier": (0x0101, 2, 0x04, b"NFC carrier")}
        for path in gen.emit(self.dir, list(want)):
            label = path.stem.split("_", 1)[1]
            d = path.read_bytes()[:32]
            magic, ver, res3, mod_id, rev, res7, caps = struct.unpack_from("<2sBBHBBI", d)
            name = d[12:30].rstrip(b"\0")
            (crc,) = struct.unpack_from("<H", d, 30)
            self.assertEqual((magic, ver, res3, res7), (b"SM", 1, 0, 0), label)
            self.assertEqual((mod_id, rev, caps, name), want[label], label)
            self.assertEqual(crc, binascii.crc_hqx(d[:30], 0xFFFF), label)

    def test_blank_and_bad_crc_chips_for_the_corrupt_chip_check(self):
        blank, bad = gen.emit(self.dir, ["blank", "bad_crc"])
        self.assertEqual(blank.read_bytes(), b"\xFF" * 256)
        d = bad.read_bytes()[:32]
        self.assertNotEqual(struct.unpack_from("<H", d, 30)[0], binascii.crc_hqx(d[:30], 0xFFFF))

    def test_unknown_label_writes_nothing(self):
        out = os.path.join(self.dir, "out")
        with self.assertRaises(KeyError):
            gen.emit(out, ["spi_breakout", "no_such_module"])
        self.assertFalse(os.path.exists(out))
        self.assertEqual(gen.main(["--emit", out, "no_such_module"]), 2)

    def test_label_without_emit_is_a_usage_error(self):
        with self.assertRaises(SystemExit):
            gen.main(["spi_breakout"])

    def test_listing_is_one_line_per_entry_in_order(self):
        lines = gen.listing()
        self.assertEqual(len(lines), len(gen.entries()))
        self.assertTrue(lines[33].split()[:2] == ["34", "spi_breakout"])

    def test_image_rejects_a_wrong_length_descriptor(self):
        with self.assertRaises(ValueError):
            gen.eeprom_image(b"\x00" * 31)


if __name__ == "__main__":
    unittest.main()
