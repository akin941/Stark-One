#!/usr/bin/env python3
"""Unit tests for scripts/test_emu.py's pure parts (no emulator needed)."""
import os
import re
import sys
import tomllib
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_emu as emu  # noqa: E402

GDB_OK = "STARK-EMU-END 3200\n"


class InjectLine(unittest.TestCase):
    def test_points_inside_input_core_update(self):
        n = emu.inject_line()
        lines = emu.INJECT_SOURCE.read_text().splitlines()
        # the line after the opening brace of input_core_update()
        start = next(i for i, l in enumerate(lines) if l.startswith("size_t input_core_update("))
        brace = next(i for i in range(start, len(lines)) if lines[i].strip() == "{")
        self.assertEqual(n, brace + 2)
        self.assertTrue(lines[n - 1].strip())


class GdbScript(unittest.TestCase):
    def test_press_windows_and_end(self):
        s = emu.gdb_script({"name": "t", "end_ms": 900,
                            "presses": [{"key": "OK", "at_ms": 400, "hold_ms": 60},
                                        {"key": "up", "at_ms": 500}]}, 4242)
        self.assertIn("target remote 127.0.0.1:4242", s)
        self.assertIn("break input_core.c:%d" % emu.inject_line(), s)
        self.assertIn("while now_ms < 900", s)
        self.assertIn("if now_ms >= 400 && now_ms < 460", s)
        self.assertIn("set var raw_bitmap = raw_bitmap | 16", s)  # OK = bit 4
        self.assertIn("if now_ms >= 500 && now_ms < 560", s)  # default hold 60 ms
        self.assertIn("set var raw_bitmap = raw_bitmap | 1", s)  # UP = bit 0
        self.assertIn(emu.END_MARK, s)

    def test_unknown_key_rejected(self):
        with self.assertRaises(SystemExit):
            emu.gdb_script({"name": "t", "end_ms": 1, "presses": [{"key": "MENU", "at_ms": 0}]},
                           1)


class CheckLog(unittest.TestCase):
    LOG = "boot: ui_ready in 159 ms\ndiag: heap=315352\nmenu: sel=1 \"x\"\napp: start about\n"

    def check(self, **scenario):
        scenario.setdefault("name", "t")
        scenario.setdefault("end_ms", 3200)
        return emu.check_log(scenario, scenario.pop("log", self.LOG),
                             scenario.pop("gdb", GDB_OK))

    def test_pass(self):
        self.assertEqual(self.check(expect=["boot: ui_ready", "app: start about"],
                                    heap_min=200000), [])

    def test_order_matters(self):
        self.assertTrue(self.check(expect=["app: start about", "boot: ui_ready"]))

    def test_missing_line(self):
        self.assertTrue(self.check(expect=["app: stop about"]))

    def test_default_forbid(self):
        self.assertTrue(self.check(log=self.LOG + "Guru Meditation Error: Core 1 panic'ed\n"))
        self.assertTrue(self.check(log=self.LOG + "ui: render failed: TIMEOUT\n"))

    def test_scenario_forbid(self):
        self.assertTrue(self.check(forbid=["menu: sel=1"]))

    def test_heap_gate(self):
        self.assertTrue(self.check(heap_min=400000))
        self.assertTrue(self.check(log="boot: ui_ready\n", heap_min=1))

    def test_drops_must_be_zero(self):
        ok = self.LOG + "diag: heap=310000 min=300000 fps=1.0 drops=0 overruns=4\n"
        self.assertEqual(self.check(log=ok), [])
        bad = self.LOG + "diag: heap=310000 min=300000 fps=1.0 drops=3 overruns=0\n"
        self.assertTrue(self.check(log=bad))

    def test_forbid_after(self):
        rule = [{"after": "menu: sel=1", "text": "app: start"}]
        self.assertTrue(self.check(forbid_after=rule))  # app: start follows sel=1
        rule = [{"after": "app: start about", "text": "menu: sel="}]
        self.assertEqual(self.check(forbid_after=rule), [])  # sel= only before
        rule = [{"after": "not in the log", "text": "x"}]
        self.assertTrue(self.check(forbid_after=rule))  # a missing anchor fails

    def test_schedule_must_complete(self):
        self.assertTrue(self.check(gdb="Remote connection closed\n"))


class Scenarios(unittest.TestCase):
    """Every committed scenario parses and names only known keys."""

    def test_committed_scenarios(self):
        files = sorted((emu.ROOT / "test/emu").glob("*.toml"))
        self.assertTrue(files)
        for f in files:
            s = tomllib.loads(f.read_text())
            self.assertTrue(re.match(r"^emu-[a-z0-9-]+$", s["name"]), f)
            self.assertGreater(s["end_ms"], 0, f)
            self.assertTrue(s["expect"], f)
            for p in s.get("presses", []):
                self.assertIn(p["key"].upper(), emu.KEYS, f)
                self.assertLess(p["at_ms"] + p.get("hold_ms", 60), s["end_ms"], f)
            emu.gdb_script(s, 1)  # generates without error


if __name__ == "__main__":
    unittest.main()
