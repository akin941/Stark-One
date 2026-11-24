#!/usr/bin/env python3
"""
emu_shards.py — split an emulator image's scenario list into N shards for parallel CI
jobs (STARK-0708).

The emulator test-image scenarios (test/emu/storage/*.toml, 39 and growing) took about an
hour run one after the other. CI now runs them as a matrix of shards, each shard a job that
downloads the same test image and runs its share of the list through scripts/test_emu.sh.
This script owns the partition so that it is deterministic and checkable:

  * Every scenario file of the image's default set lands in exactly one shard: nothing is
    dropped, nothing runs twice. `--check` proves it, and scripts/test_emu_shards.py runs
    the same proof in the lint job on every change.
  * The result depends only on the set of files and their `end_ms`, never on the order the
    filesystem lists them: files are ordered by weight then path, so adding a scenario moves
    only what the greedy rule has to move.
  * Balance: longest-processing-time-first greedy by weight, ties to the lowest shard. The
    weight of a scenario is its emulated duration (`end_ms`) plus a fixed 20 s for the boot
    every scenario pays; wall time tracks emulated time closely (docs/measurements.md).

Usage:
  emu_shards.py --image storage --shards 4 --index 2     # shard 2's scenario paths, one per line
  emu_shards.py --image storage --shards 4 --check       # every scenario in exactly one shard
  emu_shards.py --image storage --shards 4 --summary     # each shard's size and weight

Paths are repository-relative, so `scripts/test_emu.sh --image storage $(emu_shards.py ...)`
works from the repository root. Exit status 0 on success, 1 when --check fails, 2 on a usage
error (an index outside 0..shards-1, no scenarios).
"""
import argparse
import glob
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BOOT_MS = 20000  # what every scenario pays before its first press
DEFAULT_GLOBS = {
    "production": "test/emu/*.toml",
    "storage": "test/emu/storage/*.toml",
}


def scenario_files(image):
    """The repository-relative paths of an image's default scenario set, sorted."""
    return sorted(str(Path(p).relative_to(ROOT)) for p in glob.glob(str(ROOT / DEFAULT_GLOBS[image])))


def weight(path):
    """A scenario's cost: its emulated duration plus the boot it pays."""
    return int(tomllib.loads((ROOT / path).read_text())["end_ms"]) + BOOT_MS


def partition(weights, shards):
    """Split {path: weight} into `shards` lists of paths. Longest first, each onto the
    lightest shard (the lowest index on a tie); each shard's paths are then sorted by name
    so its log reads alphabetically."""
    if shards < 1:
        raise ValueError("at least one shard")
    order = sorted(weights, key=lambda p: (-weights[p], p))
    load = [0] * shards
    out = [[] for _ in range(shards)]
    for p in order:
        k = min(range(shards), key=lambda i: (load[i], i))
        out[k].append(p)
        load[k] += weights[p]
    return [sorted(s) for s in out]


def check(parts, expected):
    """Failure messages: a scenario in no shard or in more than one, an empty shard, or a
    path that is not in the expected set."""
    failures = []
    seen = {}
    for k, s in enumerate(parts):
        if not s:
            failures.append("shard %d is empty" % k)
        for p in s:
            seen.setdefault(p, []).append(k)
    for p in sorted(set(expected) - set(seen)):
        failures.append("%s is in no shard" % p)
    for p, ks in sorted(seen.items()):
        if len(ks) > 1:
            failures.append("%s is in shards %s" % (p, ks))
        if p not in expected:
            failures.append("%s is not a scenario of the image" % p)
    return failures


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--image", choices=sorted(DEFAULT_GLOBS), default="storage")
    ap.add_argument("--shards", type=int, required=True)
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--index", type=int)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--summary", action="store_true")
    args = ap.parse_args(argv)

    files = scenario_files(args.image)
    if not files or args.shards < 1:
        print("ERROR: no scenarios or no shards", file=sys.stderr)
        return 2
    weights = {p: weight(p) for p in files}
    parts = partition(weights, args.shards)
    if args.check:
        failures = check(parts, files)
        for f in failures:
            print("FAIL: " + f, file=sys.stderr)
        if not failures:
            print("OK: %d scenarios of the %s image in %d shards, each in exactly one"
                  % (len(files), args.image, args.shards))
        return 1 if failures else 0
    if args.summary:
        for k, s in enumerate(parts):
            print("shard %d: %2d scenarios, weight %6d s" % (k, len(s),
                                                            sum(weights[p] for p in s) // 1000))
        return 0
    if not 0 <= args.index < args.shards:
        print("ERROR: --index must be 0..%d" % (args.shards - 1), file=sys.stderr)
        return 2
    print("\n".join(parts[args.index]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
