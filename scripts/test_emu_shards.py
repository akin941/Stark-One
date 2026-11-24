"""Unit tests for scripts/emu_shards.py (STARK-0708): the CI shard partition of the emulator
scenarios is deterministic and every scenario lands in exactly one shard."""
import random
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import emu_shards  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
CI_SHARDS = 4  # the matrix in .github/workflows/ci.yml


class Partition(unittest.TestCase):
    def test_every_scenario_in_exactly_one_shard(self):
        for image in sorted(emu_shards.DEFAULT_GLOBS):
            files = emu_shards.scenario_files(image)
            self.assertGreater(len(files), 0, image)
            weights = {p: emu_shards.weight(p) for p in files}
            for n in range(1, 9):
                parts = emu_shards.partition(weights, n)
                flat = [p for s in parts for p in s]
                self.assertEqual(sorted(flat), files, "%s, %d shards" % (image, n))
                self.assertEqual(len(flat), len(set(flat)))
                if n <= len(files):
                    self.assertEqual(emu_shards.check(parts, files), [])

    def test_the_ci_shard_count_leaves_no_shard_empty(self):
        files = emu_shards.scenario_files("storage")
        parts = emu_shards.partition({p: emu_shards.weight(p) for p in files}, CI_SHARDS)
        self.assertEqual(len(parts), CI_SHARDS)
        self.assertTrue(all(parts))

    def test_the_workflow_matrix_matches_the_tested_shard_count(self):
        ci = (ROOT / ".github/workflows/ci.yml").read_text()
        self.assertIn("--shards %d" % CI_SHARDS, ci)
        self.assertEqual(re.findall(r"shard: \[([0-9, ]+)\]", ci),
                         [", ".join(str(i) for i in range(CI_SHARDS))])

    def test_deterministic_and_independent_of_input_order(self):
        files = emu_shards.scenario_files("storage")
        weights = {p: emu_shards.weight(p) for p in files}
        want = emu_shards.partition(weights, CI_SHARDS)
        for seed in range(5):
            shuffled = list(files)
            random.Random(seed).shuffle(shuffled)
            self.assertEqual(emu_shards.partition({p: weights[p] for p in shuffled}, CI_SHARDS),
                             want)
        self.assertEqual(emu_shards.partition(weights, CI_SHARDS), want)

    def test_balance_is_within_the_longest_scenario(self):
        # greedy longest-first: the heaviest shard is never more than the largest single
        # weight above the lightest
        files = emu_shards.scenario_files("storage")
        weights = {p: emu_shards.weight(p) for p in files}
        for n in range(2, 7):
            loads = [sum(weights[p] for p in s) for s in emu_shards.partition(weights, n)]
            self.assertLessEqual(max(loads) - min(loads), max(weights.values()), n)

    def test_check_names_a_dropped_a_doubled_and_a_foreign_scenario(self):
        expected = ["a.toml", "b.toml", "c.toml"]
        self.assertEqual(emu_shards.check([["a.toml", "b.toml"], ["c.toml"]], expected), [])
        dropped = emu_shards.check([["a.toml"], ["c.toml"]], expected)
        self.assertEqual(dropped, ["b.toml is in no shard"])
        doubled = emu_shards.check([["a.toml", "b.toml"], ["b.toml", "c.toml"]], expected)
        self.assertEqual(doubled, ["b.toml is in shards [0, 1]"])
        foreign = emu_shards.check([["a.toml", "b.toml", "x.toml"], ["c.toml"]], expected)
        self.assertEqual(foreign, ["x.toml is not a scenario of the image"])
        self.assertIn("shard 1 is empty", emu_shards.check([["a.toml", "b.toml", "c.toml"], []],
                                                           expected))

    def test_one_shard_is_the_whole_set_and_zero_shards_is_refused(self):
        weights = {"a.toml": 5, "b.toml": 1}
        self.assertEqual(emu_shards.partition(weights, 1), [["a.toml", "b.toml"]])
        with self.assertRaises(ValueError):
            emu_shards.partition(weights, 0)

    def test_ties_go_to_the_lowest_shard(self):
        weights = {"a.toml": 3, "b.toml": 3, "c.toml": 3}
        self.assertEqual(emu_shards.partition(weights, 2), [["a.toml", "c.toml"], ["b.toml"]])


class Cli(unittest.TestCase):
    def test_index_prints_the_shard_and_check_passes(self):
        import io
        from contextlib import redirect_stdout
        buf = io.StringIO()
        with redirect_stdout(buf):
            self.assertEqual(emu_shards.main(["--shards", "3", "--index", "1"]), 0)
        printed = buf.getvalue().split()
        files = emu_shards.scenario_files("storage")
        want = emu_shards.partition({p: emu_shards.weight(p) for p in files}, 3)[1]
        self.assertEqual(printed, want)
        with redirect_stdout(io.StringIO()):
            self.assertEqual(emu_shards.main(["--shards", "3", "--check"]), 0)
            self.assertEqual(emu_shards.main(["--shards", "3", "--summary"]), 0)

    def test_an_index_outside_the_shards_is_a_usage_error(self):
        import io
        from contextlib import redirect_stderr
        with redirect_stderr(io.StringIO()):
            self.assertEqual(emu_shards.main(["--shards", "3", "--index", "3"]), 2)
            self.assertEqual(emu_shards.main(["--shards", "0", "--check"]), 2)


if __name__ == "__main__":
    unittest.main()
