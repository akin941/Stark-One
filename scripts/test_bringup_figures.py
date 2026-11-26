#!/usr/bin/env python3
"""Unit tests for scripts/bringup_figures.py (STARK-0807)."""
import contextlib
import io
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bringup_figures as bf  # noqa: E402

HERE = Path(__file__).resolve().parent
# The production firmware's console from CI's esp-emulator run (STARK-0805's fixture).
FIXTURE = (HERE / "testdata/emu-v0-boot-and-menu.log").read_text()


def boot(ui_ready=185, heap=231952, refresh=(9,), diag=(), extra=()):
    lines = ["ESP-ROM:esp32s3-20210327", "boot: stark-one dev idf=v6.1 heap=295608",
             "boot: ui_ready in %d ms" % ui_ready, "diag: heap=%d" % heap]
    lines += ["display: full refresh %d ms" % ms for ms in refresh]
    lines += ["diag: heap=%d min=%d fps=%s drops=%d overruns=%d" % d for d in diag]
    return "\n".join(lines + list(extra)) + "\n"


def run(*argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = bf.main(list(argv))
    return code, out.getvalue(), err.getvalue()


def row(summary, prefix):
    return next(r for r in bf.rows(summary) if r[0].startswith(prefix))


class Figures(unittest.TestCase):
    def test_real_firmware_log(self):
        s = bf.summarise([FIXTURE])
        self.assertEqual(s["boots"], 1)
        self.assertEqual(s["ui_ready"], [185])
        self.assertEqual(s["root_heap"], [231952])
        self.assertEqual(s["refresh"], [9, 9, 9])
        self.assertEqual(s["fps"], [6.0])
        self.assertEqual(s["drops"], 0)
        self.assertEqual(s["heap_min"], 229940)
        self.assertFalse(any(r[3] == "FAIL" for r in bf.rows(s)))

    def test_monitor_output_and_noise_before_the_boot(self):
        text = "boot: ui_ready in 999 ms\n" + "".join(
            "\x1b[0;32m%s\x1b[0m\r\n" % line for line in FIXTURE.splitlines())
        self.assertEqual(bf.summarise([text])["ui_ready"], [185])

    def test_several_boots_and_files(self):
        s = bf.summarise([boot(150) + boot(200), boot(170, heap=220000)])
        self.assertEqual(s["boots"], 3)
        self.assertEqual(row(s, "`boot: ui_ready`")[2], "worst 200, median 170 ms (n=3)")
        self.assertEqual(row(s, "Free internal heap")[2], "lowest 220000 B (n=3)")

    def test_gates(self):
        self.assertEqual(row(bf.summarise([boot(601)]), "`boot: ui_ready`")[3], "FAIL")
        self.assertEqual(row(bf.summarise([boot(600)]), "`boot: ui_ready`")[3], "pass")
        self.assertEqual(row(bf.summarise([boot(refresh=(9, 61))]), "Full-screen")[3], "FAIL")
        self.assertEqual(row(bf.summarise([boot(heap=199999)]), "Free internal")[3], "FAIL")
        drops = boot(diag=[(230000, 229000, "6.0", 1, 0)])
        self.assertEqual(row(bf.summarise([drops]), "Dropped")[3], "FAIL")

    def test_overruns_and_heap_are_per_boot(self):
        d = [(230000, 229000, "20.0", 0, 3), (230100, 229000, "21.5", 0, 3)]
        s = bf.summarise([boot(diag=d), boot(diag=[(1, 1, "1.0", 0, 9), (1, 1, "1.0", 0, 10)])])
        self.assertEqual(s["overruns"], [0, 1])  # added during each boot, not the counter
        self.assertEqual(row(s, "Frame overruns")[3], "FAIL")
        self.assertEqual(s["heap_delta"], [100, 0])
        self.assertEqual(row(s, "`diag:` fps")[2], "1 to 21.5 (n=4)")
        self.assertIsNone(row(s, "`diag:` fps")[3])  # never a verdict
        many = bf.summarise([boot(diag=[(1000, 1, "1.0", 0, 0), (1000 + k, 1, "1.0", 0, 0)])
                             for k in (5, -700, 20, 0, 3)])
        self.assertEqual(row(many, "Heap, first")[2], "largest change -700 B (n=5)")

    def test_missing_figures_are_not_gated(self):
        s = bf.summarise(["boot: stark-one dev idf=v6.1 heap=1\n"])
        for metric, _target, value, verdict in bf.rows(s):
            self.assertEqual(value, "—", metric)
            self.assertIsNone(verdict, metric)

    def test_analyser_figures_never_come_from_the_log(self):
        text, _ = bf.report(bf.summarise([FIXTURE]))
        for name, _target, where in bf.ANALYSER_ONLY:
            self.assertIn("* %s" % name, text)
            self.assertIn(where, text)
        self.assertNotIn("First pixel", text.split("Not in the log")[0])


class Cli(unittest.TestCase):
    def write(self, d, name, text):
        p = Path(d) / name
        p.write_text(text)
        return str(p)

    def test_exit_status_and_compare_column(self):
        with tempfile.TemporaryDirectory() as d:
            ok = self.write(d, "ok.log", boot(300))
            slow = self.write(d, "slow.log", boot(700))
            emu = self.write(d, "emu.log", FIXTURE)
            code, out, _ = run(ok, "--compare", emu)
            self.assertEqual(code, 0)
            self.assertIn("| Metric | Target | Hardware | Verdict | Simulated |", out)
            self.assertIn("| 300 ms | pass | 185 ms |", out)
            code, out, _ = run(ok, slow)
            self.assertEqual(code, 1)
            self.assertIn("worst 700, median 500 ms (n=2) | FAIL", out)
            empty = self.write(d, "empty.log", "no firmware here\n")
            code, _, err = run(empty)
            self.assertEqual(code, 2)
            self.assertIn("no `boot: stark-one` line", err)
            self.assertEqual(run(ok, "--compare", empty)[0], 2)


if __name__ == "__main__":
    unittest.main()
