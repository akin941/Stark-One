#!/usr/bin/env python3
"""
check_emu_image.py — prove the emulator storage test image is the production
firmware with exactly two substitutions: the storage medium (ADR-0018 §3,
TASKS.md STARK-0201) and the NFC transport (ADR-0020 §3, STARK-0402).

Compares two finished builds:
  * the component sets must be identical;
  * every component must come from the same place with the same sources,
    requirements and include directories — except `stark_storage_medium` and
    `stark_nfc_transport`, which must come from test/emu/storage_image/components/;
    managed (registry) components are compared by their name, since each
    project keeps its own managed_components/ copy;
  * the resolved sdkconfig must be identical;
  * the test image's partition table must be the production table plus
    partitions of its own (rows are compared, comments ignored).

Usage:
  check_emu_image.py [--prod build] [--emu build/emu-storage]
                     [--prod-partitions partitions.csv]
                     [--emu-partitions test/emu/storage_image/partitions.csv]

Exit status 0 when the images differ only as allowed, 1 otherwise.
"""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SUBSTITUTED = ("stark_storage_medium", "stark_nfc_transport")
SUBSTITUTE_DIRS = {name: "test/emu/storage_image/components/" + name for name in SUBSTITUTED}
COMPARED_FIELDS = ("sources", "include_dirs", "reqs", "priv_reqs")


def normalise(path, root, build_dir):
    """A component path, made comparable across the two projects: generated
    files relative to their build directory, everything else relative to the
    repository, with each project's managed_components/ folded together."""
    if path.startswith(build_dir + "/"):
        return "<build>/" + path[len(build_dir) + 1:]
    rel = path[len(root):].lstrip("/") if path.startswith(root) else path
    marker = "managed_components/"
    return marker + rel.split(marker, 1)[1] if marker in rel else rel


def compare_components(prod, emu, root):
    """Failure messages for two project_description.json dicts; root is the
    repository both builds' component paths are under."""
    failures = []
    a = {c for c in prod["build_components"] if c}
    b = {c for c in emu["build_components"] if c}
    if a != b:
        failures.append("component sets differ: only production %s, only test image %s"
                        % (sorted(a - b), sorted(b - a)))
    ia, ib = prod["build_component_info"], emu["build_component_info"]
    for name in sorted(a & b):
        da = normalise(ia[name]["dir"], root, prod["build_dir"])
        db = normalise(ib[name]["dir"], root, emu["build_dir"])
        if name in SUBSTITUTED:
            if not db.endswith(SUBSTITUTE_DIRS[name]):
                failures.append("%s not substituted: test image uses %s" % (name, db))
            continue
        if da != db:
            failures.append("%s comes from %s in production but %s in the test image"
                            % (name, da, db))
            continue
        for field in COMPARED_FIELDS:
            fa = [normalise(x, root, prod["build_dir"]) for x in ia[name].get(field) or []]
            fb = [normalise(x, root, emu["build_dir"]) for x in ib[name].get(field) or []]
            if sorted(fa) != sorted(fb):
                failures.append("%s: %s differ" % (name, field))
    return failures


def compare_sdkconfig(prod, emu):
    keys = sorted(k for k in set(prod) | set(emu) if prod.get(k) != emu.get(k))
    return ["sdkconfig differs: %s" % ", ".join(keys)] if keys else []


def partition_rows(text):
    rows = []
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            rows.append(tuple(cell.strip() for cell in line.split(",")))
    return rows


def compare_partitions(prod_text, emu_text):
    prod, emu = partition_rows(prod_text), partition_rows(emu_text)
    if emu[:len(prod)] != prod:
        return ["test partition table does not start with the production table"]
    return []


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--prod", default=str(ROOT / "build"))
    ap.add_argument("--emu", default=str(ROOT / "build/emu-storage"))
    ap.add_argument("--prod-partitions", default=str(ROOT / "partitions.csv"))
    ap.add_argument("--emu-partitions",
                    default=str(ROOT / "test/emu/storage_image/partitions.csv"))
    args = ap.parse_args(argv)

    def load(build, name):
        return json.loads((Path(build) / name).read_text())

    prod_desc = load(args.prod, "project_description.json")
    emu_desc = load(args.emu, "project_description.json")
    # Production's project_path is the repository both builds draw from.
    failures = compare_components(prod_desc, emu_desc, prod_desc["project_path"])
    failures += compare_sdkconfig(load(args.prod, "config/sdkconfig.json"),
                                  load(args.emu, "config/sdkconfig.json"))
    failures += compare_partitions(Path(args.prod_partitions).read_text(),
                                   Path(args.emu_partitions).read_text())
    for f in failures:
        print("FAIL: " + f, file=sys.stderr)
    if failures:
        return 1
    print("OK: the storage test image differs from production only in %s "
          "(and its extra partition)." % " and ".join(SUBSTITUTED))
    return 0


if __name__ == "__main__":
    sys.exit(main())
