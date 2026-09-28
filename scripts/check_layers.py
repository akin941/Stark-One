#!/usr/bin/env python3
"""
check_layers.py — enforce the component layer rules (ARCHITECTURE.md §2, §10).

Parses the REQUIRES / PRIV_REQUIRES lists of every idf_component_register()
in components/*/, apps/*/ and main/, and fails on any dependency the layer
table below does not allow. Standard library only; STARK-0100.

Rules (ARCHITECTURE.md §10 documents the same table):
  * a component may require a strictly lower layer, `stark_err` (universal),
    a platform component (anything not stark_* / app_*), or an edge on the
    explicit SIDEWAYS allowlist;
  * PURE components (all-pure L2, and stark_err) may require no platform
    component;
  * only `main` (the composition root) may require an app_*; apps never
    require each other;
  * every stark_* component must appear in LAYERS — a new component fails as
    "unclassified" until its layer is decided and written down here and in
    ARCHITECTURE.md §10, in the same change.

Usage:  scripts/check_layers.py [--root DIR]
"""
import argparse
import re
import sys
from pathlib import Path

UNIVERSAL = "stark_err"

LAYERS = {
    "stark_board": 0,
    "stark_hal": 1,
    "stark_log": 1,
    "stark_gfx": 2,
    "stark_event": 3,  # L2 core + L3 port
    "stark_input": 3,  # L2 core + L3 service
    "stark_display": 3,
    "stark_buzzer": 3,
    "stark_diag": 3,
    "stark_ui": 4,
    "stark_app": 4,
}
APP_LAYER = 5
ROOT = "main"

# Same-layer edges that are part of the design (ARCHITECTURE.md §10).
SIDEWAYS = {
    ("stark_input", "stark_event"),  # the input service publishes key events
    ("stark_app", "stark_ui"),  # the launcher is a stark_ui menu
    ("stark_diag", "stark_event"),  # event-drop counters + the sample event (STARK-0109)
}

PURE = {"stark_err", "stark_gfx"}

KEYWORDS = {"SRCS", "SRC_DIRS", "EXCLUDE_SRCS", "INCLUDE_DIRS", "PRIV_INCLUDE_DIRS",
            "REQUIRES", "PRIV_REQUIRES", "LDFRAGMENTS", "REQUIRED_IDF_TARGETS",
            "EMBED_FILES", "EMBED_TXTFILES", "KCONFIG", "KCONFIG_PROJBUILD",
            "WHOLE_ARCHIVE"}


def parse_requires(cmake_text):
    """Every token after REQUIRES/PRIV_REQUIRES inside idf_component_register()."""
    text = re.sub(r"#[^\n]*", "", cmake_text)
    m = re.search(r"idf_component_register\s*\((.*?)\)", text, re.S)
    if not m:
        return None
    deps, mode = [], None
    for tok in m.group(1).split():
        tok = tok.strip('"')
        if tok in KEYWORDS:
            mode = tok if tok in ("REQUIRES", "PRIV_REQUIRES") else None
        elif mode and tok:
            deps.append(tok)
    return deps


def layer_of(name):
    """Layer number, 'root', 'platform' or None (unclassified stark_*)."""
    if name == ROOT:
        return "root"
    if name.startswith("app_"):
        return APP_LAYER
    if name.startswith("stark_"):
        return LAYERS.get(name) if name != UNIVERSAL else "universal"
    return "platform"


def check_component(name, deps):
    errors = []
    own = layer_of(name)
    if own is None:
        return ["%s: unclassified component — add it to LAYERS in scripts/check_layers.py "
                "and ARCHITECTURE.md §10" % name]
    for dep in deps:
        dl = layer_of(dep)
        if own == "root" or dep == UNIVERSAL:
            continue
        if dl == "platform":
            if name in PURE:
                errors.append("%s -> %s: a pure component may not require a platform "
                              "component" % (name, dep))
            continue
        if dep.startswith("app_"):
            errors.append("%s -> %s: only main may require an app" % (name, dep))
            continue
        if dl is None:
            errors.append("%s -> %s: unclassified component" % (name, dep))
            continue
        if own == "universal":
            errors.append("%s -> %s: stark_err may not depend on anything" % (name, dep))
        elif dl < own or (name, dep) in SIDEWAYS:
            continue
        elif dl == own:
            errors.append("%s -> %s: sideways L%d dependency not in the allowlist"
                          % (name, dep, own))
        else:
            errors.append("%s -> %s: upward dependency (L%d -> L%d)" % (name, dep, own, dl))
    return errors


def main(argv=None):
    ap = argparse.ArgumentParser(description="Check component layer rules.")
    ap.add_argument("--root", default=Path(__file__).resolve().parent.parent, type=Path)
    args = ap.parse_args(argv)

    cmakes = sorted(args.root.glob("components/*/CMakeLists.txt")) + \
        sorted(args.root.glob("apps/*/CMakeLists.txt")) + [args.root / "main/CMakeLists.txt"]
    errors, checked = [], 0
    for cmake in cmakes:
        name = cmake.parent.name
        deps = parse_requires(cmake.read_text())
        if deps is None:
            errors.append("%s: no idf_component_register() found" % cmake)
            continue
        checked += 1
        errors += ["%s: %s" % (cmake.relative_to(args.root), e)
                   for e in check_component(name, deps)]
    for e in errors:
        print("ERROR: " + e, file=sys.stderr)
    if errors:
        return 1
    print("OK: %d components respect the layer rules (ARCHITECTURE.md §2, §10)." % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
