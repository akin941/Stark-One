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

    def test_heap_delta(self):
        log = ("diag: heap=300000\n" "diag: heap=310000 min=300000 fps=1.0 drops=0 overruns=0\n"
               "diag: heap=310500 min=300000 fps=1.0 drops=0 overruns=0\n")
        self.assertEqual(self.check(log=log, heap_delta_max=1024), [])  # boot line ignored
        self.assertTrue(self.check(log=log, heap_delta_max=100))
        self.assertTrue(self.check(log="diag: heap=1 min=1 fps=0.0 drops=0 overruns=0\n",
                                   heap_delta_max=1024))  # needs two lines

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
        files = sorted((emu.ROOT / "test/emu").rglob("*.toml"))  # soak/ included
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
            # storage-image scenarios live in test/emu/storage/ (or soak/),
            # and nothing in storage/ runs on the production image
            image = s.get("image", "production")
            if f.parent.name == "storage":
                self.assertEqual(image, "storage", f)
            elif f.parent.name != "soak":
                self.assertEqual(image, "production", f)


class Pokes(unittest.TestCase):
    def test_poke_fires_once(self):
        s = emu.gdb_script({"name": "t", "end_ms": 900, "image": "storage",
                            "pokes": [{"at_ms": 400, "var": "emu_medium_present",
                                       "value": 0}]}, 1)
        self.assertIn("set $poke0 = 0", s)
        self.assertIn("if $poke0 == 0 && now_ms >= 400", s)
        self.assertIn("set var emu_medium_present = 0", s)
        self.assertIn("set $poke0 = 1", s)

    def test_pokes_need_the_storage_image(self):
        with self.assertRaises(SystemExit):
            emu.gdb_script({"name": "t", "end_ms": 900,
                            "pokes": [{"at_ms": 1, "var": "emu_medium_present", "value": 0}]}, 1)

    def test_unknown_variable(self):
        with self.assertRaises(SystemExit):
            emu.gdb_script({"name": "t", "end_ms": 900, "image": "storage",
                            "pokes": [{"at_ms": 1, "var": "s_core", "value": 0}]}, 1)


class Control(unittest.TestCase):
    SCEN = {"name": "t", "end_ms": 900,
            "control": [{"at_ms": 300, "command": "reset"},
                        {"at_ms": 50, "phase": 1, "command": "erase-region 0x9000 0x6000"},
                        {"at_ms": 400, "phase": 1, "command": "reset"}],
            "presses": [{"key": "OK", "at_ms": 100}, {"key": "UP", "at_ms": 100, "phase": 2}]}

    def test_split_phases(self):
        phases = emu.split_phases(self.SCEN)
        self.assertEqual([p["end_ms"] for p in phases], [300, 400, 900])
        self.assertEqual([len(p["presses"]) for p in phases], [1, 0, 1])
        self.assertEqual([len(p["control"]) for p in phases], [0, 1, 0])  # resets removed

    def test_no_reset_is_one_phase(self):
        self.assertEqual(len(emu.split_phases({"name": "t", "end_ms": 5})), 1)

    def test_reset_in_wrong_phase(self):
        with self.assertRaises(SystemExit):
            emu.split_phases({"name": "t", "end_ms": 9,
                              "control": [{"at_ms": 1, "phase": 1, "command": "reset"}]})

    def test_control_command_in_gdb(self):
        s = emu.gdb_script(emu.split_phases(self.SCEN)[1], 1, 7777)
        self.assertIn("if $ctl0 == 0 && now_ms >= 50", s)
        self.assertIn("create_connection(('127.0.0.1', 7777))", s)
        self.assertIn("b'erase-region 0x9000 0x6000\\n'", s)
        self.assertNotIn("reset", s)


class ForbidBetween(unittest.TestCase):
    RULE = {"forbid_between": [{"after": "ejected", "before": "mount requested",
                                "text": "mounted x"}]}

    def run_check(self, log):
        return emu.check_log(dict(self.RULE, name="t", end_ms=1), log, GDB_OK)

    def test_clean_window(self):
        self.assertEqual(self.run_check("ejected\nmount requested\nmounted x\n"), [])

    def test_hit_inside_window(self):
        f = self.run_check("ejected\nmounted x\nmount requested\n")
        self.assertTrue(any("between" in m for m in f), f)

    def test_missing_anchors(self):
        self.assertTrue(any("anchor not found" in m for m in self.run_check("x\n")))
        self.assertTrue(any("end not found" in m for m in self.run_check("ejected\n")))


class ExpectRe(unittest.TestCase):
    def check(self, log):
        return emu.check_log({"name": "t", "end_ms": 1, "expect": ["start"],
                              "expect_re": [r"ok frames=[1-9]"]}, log, GDB_OK)

    def test_matches_after_expect(self):
        self.assertEqual(self.check("start\nok frames=12\n"), [])

    def test_zero_or_before_expect_fails(self):
        self.assertTrue(self.check("start\nok frames=0\n"))
        self.assertTrue(self.check("ok frames=3\nstart\n"))


class Images(unittest.TestCase):
    def test_mismatch(self):
        self.assertIsNone(emu.image_mismatch({}, "production"))
        self.assertIsNone(emu.image_mismatch({"image": "storage"}, "storage"))
        self.assertIn("needs the storage image",
                      emu.image_mismatch({"image": "storage"}, "production"))
        self.assertIn("needs the production image", emu.image_mismatch({}, "storage"))
        self.assertIn("unknown image", emu.image_mismatch({"image": "sd"}, "production"))

    def test_default_sets_are_disjoint(self):
        self.assertEqual(emu.IMAGES["production"][2], "test/emu/*.toml")
        self.assertEqual(emu.IMAGES["storage"][2], "test/emu/storage/*.toml")


if __name__ == "__main__":
    unittest.main()
