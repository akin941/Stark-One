#!/usr/bin/env python3
"""Unit tests for scripts/fmt.sh's file selection (STARK-0801).

fmt.sh is copied into a throwaway tree with a stub `clang-format` first on PATH
that records the files it is given, so no real formatter is needed.
"""
import os
import shutil
import stat
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
FMT = os.path.join(HERE, "fmt.sh")

STUB = """#!/bin/sh
for a in "$@"; do
    case "$a" in
        -*) ;;
        *) printf '%s\\n' "$a" >> "$STUB_LOG" ;;
    esac
done
exit "${STUB_RC:-0}"
"""

OURS = ["main/stark_main.c", "components/stark_x/include/stark_x.h", "test/host/t.c"]
NOT_OURS = [
    ".venv/include/python3.14/Python.h",
    "apps/x/.venv/lib/y.h",
    "build/gen.c",
    "build-emu/gen.c",
    "managed_components/lib/z.c",
    ".git/hooks/h.c",
]


class FmtSelectionTest(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="stark-fmt-")
        os.makedirs(os.path.join(self.root, "scripts"))
        shutil.copy(FMT, os.path.join(self.root, "scripts", "fmt.sh"))
        for rel in OURS + NOT_OURS:
            path = os.path.join(self.root, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as f:
                f.write("int x;\n")
        self.bin = os.path.join(self.root, "stubbin")
        os.makedirs(self.bin)
        stub = os.path.join(self.bin, "clang-format")
        with open(stub, "w") as f:
            f.write(STUB)
        os.chmod(stub, os.stat(stub).st_mode | stat.S_IXUSR)
        self.log = os.path.join(self.root, "stub.log")

    def tearDown(self):
        shutil.rmtree(self.root)

    def run_fmt(self, *args, rc=0):
        env = dict(os.environ, PATH=self.bin + os.pathsep + os.environ["PATH"],
                   STUB_LOG=self.log, STUB_RC=str(rc))
        return subprocess.run(["bash", os.path.join(self.root, "scripts", "fmt.sh"), *args],
                              env=env, capture_output=True, text=True, check=False)

    def seen(self):
        with open(self.log) as f:
            return sorted(os.path.relpath(line.strip(), self.root) for line in f if line.strip())

    def test_check_formats_only_our_sources(self):
        res = self.run_fmt("--check")
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertEqual(self.seen(), sorted(OURS))

    def test_apply_formats_only_our_sources(self):
        res = self.run_fmt()
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertEqual(self.seen(), sorted(OURS))
        self.assertIn("Formatted 3 file(s).", res.stdout)

    def test_check_fails_when_formatter_reports_a_diff(self):
        res = self.run_fmt("--check", rc=1)
        self.assertEqual(res.returncode, 1)
        self.assertIn("need formatting", res.stderr)

    def test_path_with_spaces_is_one_file(self):
        path = os.path.join(self.root, "dir with space", "a b.c")
        os.makedirs(os.path.dirname(path))
        with open(path, "w") as f:
            f.write("int y;\n")
        res = self.run_fmt("--check")
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("dir with space/a b.c", self.seen())


if __name__ == "__main__":
    unittest.main()
