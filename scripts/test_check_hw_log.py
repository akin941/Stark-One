#!/usr/bin/env python3
"""Unit tests for scripts/check_hw_log.py (STARK-0805)."""
import contextlib
import io
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_hw_log as hw  # noqa: E402

HERE = Path(__file__).resolve().parent
# The production firmware's console from CI's esp-emulator run of this scenario
# (main 6b492f5), with esp-emu's own banner and [INFO] lines removed.
FIXTURE = HERE / "testdata/emu-v0-boot-and-menu.log"
SCENARIO = hw.ROOT / "test/emu/v0-boot-and-menu.toml"


def as_monitor(log):
    """The same lines as idf.py monitor prints them: CRLF, ANSI colours."""
    return "".join("\x1b[0;32m%s\x1b[0m\r\n" % line for line in log.splitlines())


def run(*argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = hw.main(list(argv))
    return code, out.getvalue(), err.getvalue()


class Check(unittest.TestCase):
    def setUp(self):
        self.scenario = hw.load(SCENARIO)
        self.log = FIXTURE.read_text()

    def test_real_firmware_log_passes(self):
        self.assertEqual(hw.check(self.scenario, self.log), [])

    def test_monitor_formatting_is_ignored(self):
        self.assertEqual(hw.check(self.scenario, as_monitor(self.log)), [])
        # an end-anchored regex (v07-lab-boot's `lab: pins 38 39$`) sees the bare line
        anchored = dict(self.scenario, expect=["boot: ui_ready in "],
                        expect_re=["app: stop about$"])
        self.assertEqual(hw.check(anchored, as_monitor(self.log)), [])
        self.assertTrue(hw.emu.check_log(anchored, as_monitor(self.log), None))

    def test_same_rules_as_the_emulator(self):
        # a missing line, a wrong order, a panic, a drop, a low heap all fail
        self.assertTrue(hw.check(self.scenario, self.log.replace("app: stop about", "")))
        lines = self.log.splitlines()
        i, j = lines.index("app: start about"), lines.index('menu: sel=2 "Diagnostics"')
        lines[i], lines[j] = lines[j], lines[i]
        self.assertTrue(hw.check(self.scenario, "\n".join(lines)))
        self.assertTrue(hw.check(self.scenario, self.log + "Guru Meditation Error\n"))
        self.assertTrue(hw.check(self.scenario, self.log.replace("drops=0", "drops=1")))
        self.assertTrue(hw.check(dict(self.scenario, heap_min=400000), self.log))

    def test_no_gdb_schedule_needed(self):
        # the emulator check fails a log without its GDB end mark; hardware has none
        self.assertTrue(hw.emu.check_log(self.scenario, self.log, ""))
        self.assertEqual(hw.check(self.scenario, self.log), [])

    def test_phase_markers_not_expected(self):
        s = dict(self.scenario, expect=["boot: ui_ready in ", "--- phase 1 ---",
                                        "app: start about"])
        self.assertEqual(hw.check(s, self.log), [])


class Replayable(unittest.TestCase):
    def test_reasons(self):
        self.assertEqual(hw.not_replayable({"control": [{"at_ms": 1, "command": "reset"}]}), [])
        for s in ({"image": "storage"}, {"rmt_loopback": "0:2"}, {"uart1_peer": "echo"},
                  {"pokes": [{"at_ms": 1, "var": "x", "value": 0}]},
                  {"control": [{"at_ms": 1, "command": "erase-region 0x9000 0x6000"}]}):
            self.assertTrue(hw.not_replayable(s), s)

    def test_committed_set(self):
        ok = {f.name for f in hw.all_scenarios() if not hw.not_replayable(hw.load(f))}
        # every replayable one is a production-image scenario, and every
        # production-image scenario without an emulator-only feature is replayable
        self.assertIn("v0-boot-and-menu.toml", ok)
        self.assertIn("v02-settings-persist.toml", ok)
        self.assertIn("v01-soak.toml", ok)
        for name in ("v03-ir-loopback.toml", "v07-uart-prod.toml", "v02-settings-store.toml",
                     "v02-soak.toml", "v02-files.toml"):
            self.assertNotIn(name, ok)
        for f in hw.all_scenarios():
            s = hw.load(f)
            if f.name in ok:
                self.assertEqual(s.get("image", "production"), "production", f)
            for step in (hw.steps(s) if f.name in ok else []):
                self.assertTrue(step[0].isupper() and step.endswith("."), (f, step))


class Steps(unittest.TestCase):
    def scenario(self, presses, **kw):
        return dict({"name": "t", "end_ms": 9000, "expect": ["x"], "presses": presses}, **kw)

    def test_presses_holds_and_chords(self):
        s = self.steps([{"key": "down", "at_ms": 300},
                        {"key": "BACK", "at_ms": 600, "hold_ms": 700},
                        {"key": "UP", "at_ms": 1500, "hold_ms": 450},
                        {"key": "OK", "at_ms": 2000, "hold_ms": 200},
                        {"key": "BACK", "at_ms": 2030, "hold_ms": 170}])
        self.assertIn("Press DOWN.", s)
        self.assertIn("Hold BACK about 700 ms (long press).", s)
        self.assertIn("Hold UP about 450 ms.", s)
        self.assertIn("Press OK and BACK together (hold about 200 ms).", s)

    def test_long_arrow_repeats(self):
        s = self.steps([{"key": "DOWN", "at_ms": 300, "hold_ms": 2000}])
        self.assertIn("Hold DOWN about 2000 ms (long press, auto-repeats).", s)

    def test_resets_waits_and_order(self):
        s = self.steps([{"key": "OK", "at_ms": 300},
                        {"key": "OK", "at_ms": 2500},
                        {"key": "UP", "at_ms": 300, "phase": 1}],
                       control=[{"at_ms": 4000, "command": "reset"}])
        self.assertEqual(s.count("Press RESET (EN) and wait for `boot: ui_ready`."), 2)
        self.assertLess(s.index("Press OK."), s.index("Wait about 2 s."))
        reset = len(s) - 1 - s[::-1].index("Press RESET (EN) and wait for `boot: ui_ready`.")
        self.assertEqual(s[reset + 1], "Press UP.")
        self.assertTrue(s[-1].startswith("Wait 12 s with no key pressed"))

    def test_defaults_need_erased_flash_first(self):
        s = self.steps([], expect=["settings: loaded brightness=100 (defaults)"])
        self.assertTrue(s[0].startswith("Erase the flash first"))
        self.assertFalse(any("Erase" in x for x in self.steps([])))

    def test_heap_delta_waits_out_the_scenario(self):
        s = self.steps([{"key": "OK", "at_ms": 300}], end_ms=62940, heap_delta_max=1024)
        self.assertTrue(s[-1].startswith("Wait 64 s"), s[-1])

    def steps(self, presses, **kw):
        return hw.steps(self.scenario(presses, **kw))


class Cli(unittest.TestCase):
    def test_check_pass_and_fail(self):
        code, out, _ = run(str(SCENARIO), str(FIXTURE))
        self.assertEqual(code, 0)
        self.assertIn("PASS: emu-v0-boot-and-menu", out)
        with tempfile.TemporaryDirectory() as d:
            bad = Path(d) / "bad.log"
            bad.write_text(FIXTURE.read_text().replace("app: stop about", ""))
            code, _, err = run(str(SCENARIO), str(bad))
            self.assertEqual(code, 1)
            self.assertIn("app: stop about", err)

    def test_steps_and_list(self):
        code, out, _ = run("--steps", str(SCENARIO))
        self.assertEqual(code, 0)
        self.assertIn(" 3. Press DOWN.", out)
        code, out, _ = run("--list")
        self.assertEqual(code, 0)
        self.assertIn("test/emu/v03-ir-loopback.toml", out)
        self.assertIn("not replayable", out)

    def test_not_replayable_and_usage(self):
        code, _, err = run("--steps", str(hw.ROOT / "test/emu/v03-ir-loopback.toml"))
        self.assertEqual(code, 2)
        self.assertIn("cannot be replayed", err)
        for argv in ([str(SCENARIO)], ["--list", str(SCENARIO)], ["--steps"]):
            with self.assertRaises(SystemExit) as cm, \
                    contextlib.redirect_stderr(io.StringIO()):
                hw.main(argv)
            self.assertEqual(cm.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
