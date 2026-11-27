#!/usr/bin/env python3
"""Unit tests for scripts/check_caps.py (STARK-0902)."""
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_caps as cc  # noqa: E402

GOOD_CAPS = """#pragma once
#define STARK_CAP_IR_TX (1u << 0) /* x */
#define STARK_CAP_NFC   (1u << 1) /* y */
#define STARK_CAPS_KNOWN (STARK_CAP_IR_TX | STARK_CAP_NFC)
"""


class ParseUnit(unittest.TestCase):
    def test_parse_real_header(self):
        bits, known = cc.parse_caps(cc.CAPS_H.read_text())
        self.assertEqual(bits["STARK_CAP_IR_TX"], 0)
        self.assertEqual(bits["STARK_CAP_SUBGHZ"], 5)
        self.assertEqual(set(bits), known)  # KNOWN lists exactly the defined bits

    def test_real_tree_passes(self):
        self.assertEqual(cc.check(), [])


class Tree(unittest.TestCase):
    """Point the module at a synthetic tree and assert the findings."""

    def build(self, d, caps=GOOD_CAPS, app=('ir', '0'), src=None):
        root = Path(d)
        inc = root / "components" / "stark_module" / "include"
        inc.mkdir(parents=True)
        (inc / "stark_caps.h").write_text(caps)
        appdir = root / "apps" / ("app_%s" % app[0])
        appdir.mkdir(parents=True)
        (appdir / ("app_%s.c" % app[0])).write_text(
            'static const int x = {\n.id = "%s",\n.caps_required = %s,\n};\n' % (app[0], app[1]))
        (root / "main").mkdir()
        if src is not None:
            (root / "main" / "extra.c").write_text(src)
        cc.PROJECT_ROOT = root
        cc.CAPS_H = inc / "stark_caps.h"

    def run_in(self, **kw):
        saved = (cc.PROJECT_ROOT, cc.CAPS_H)
        try:
            with tempfile.TemporaryDirectory() as d:
                self.build(d, **kw)
                return cc.check()
        finally:
            cc.PROJECT_ROOT, cc.CAPS_H = saved

    def test_good_tree(self):
        self.assertEqual(self.run_in(app=('ir', 'STARK_CAP_IR_TX')), [])

    def test_missing_from_known(self):
        caps = GOOD_CAPS.replace(" | STARK_CAP_NFC)", ")")  # NFC defined but dropped from KNOWN
        errs = self.run_in(caps=caps)
        self.assertTrue(any("missing from STARK_CAPS_KNOWN" in e for e in errs), errs)

    def test_unknown_in_known(self):
        caps = GOOD_CAPS.replace("STARK_CAP_NFC)", "STARK_CAP_NFC | STARK_CAP_GHOST)")
        errs = self.run_in(caps=caps)
        self.assertTrue(any("STARK_CAP_GHOST" in e and "not a defined bit" in e for e in errs), errs)

    def test_shared_bit(self):
        caps = GOOD_CAPS.replace("STARK_CAP_NFC   (1u << 1)", "STARK_CAP_NFC   (1u << 0)")
        errs = self.run_in(caps=caps)
        self.assertTrue(any("bit 0 is shared" in e for e in errs), errs)

    def test_app_requires_undefined(self):
        errs = self.run_in(app=('ir', 'STARK_CAP_SUBGHZ'))  # not defined in GOOD_CAPS
        self.assertTrue(any("requires STARK_CAP_SUBGHZ" in e for e in errs), errs)

    def test_ifdef_feature_flag_is_rejected(self):
        for macro in ("#ifdef WIFI\n#endif\n", "#if defined(STM32)\n#endif\n",
                      "#ifndef RP2350\n#endif\n", "#if LINUX\n#endif\n"):
            errs = self.run_in(src=macro)
            self.assertTrue(any("feature/target macro" in e for e in errs), (macro, errs))

    def test_plain_ifdef_is_allowed(self):
        self.assertEqual(self.run_in(src="#ifdef __cplusplus\n#endif\n"), [])


if __name__ == "__main__":
    unittest.main()
