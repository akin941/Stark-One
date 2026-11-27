#!/usr/bin/env python3
"""
scripts/check_caps.py — the capability matrix is consistent and nothing is gated by a
per-feature or per-target build flag (TASKS.md STARK-0902, ADR-0025, docs/PLATFORM.md).

`components/stark_module/include/stark_caps.h` is the single definition of the capability
bits; each app declares what it needs once, in its `.caps_required`. This script is to that
pair what `check_pins.py` is to the pin map: it invents no capability of its own, it checks
that the committed sources agree and that the no-build-flag rule (ROADMAP "Platform" exit 3,
ADR-0010/0025) holds. It does not duplicate the matrix — it derives it from the code.

Checks (any failure exits 1):
  1. Every `STARK_CAP_*` bit in stark_caps.h is a distinct single bit (`1u << n`, unique n).
  2. `STARK_CAPS_KNOWN` names exactly the defined bits — no missing bit, no unknown name,
     no duplicate. A new capability must be added to the mask in the same change.
  3. Every `STARK_CAP_*` token an app names in `.caps_required` is a defined bit (`0` means
     no requirement and is allowed).
  4. No source under components/, apps/, main/ gates code with `#if[def]` on a feature or
     target macro (WIFI / STM32 / RP2350 / LINUX): the capability matrix gates at run time,
     builds do not fork the shared logic.

Usage:
  scripts/check_caps.py            run the checks
  scripts/check_caps.py --list     print the (app -> capabilities) matrix and exit 0
Exit code 0 on success, 1 on a finding, 2 on a usage error.
"""
import argparse
import re
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
CAPS_H = PROJECT_ROOT / "components" / "stark_module" / "include" / "stark_caps.h"
SRC_DIRS = ("components", "apps", "main")

BIT_RE = re.compile(r"#define\s+(STARK_CAP_[A-Z0-9_]+)\s+\(1u\s*<<\s*(\d+)\)")
KNOWN_RE = re.compile(r"#define\s+STARK_CAPS_KNOWN\b(.*?)(?=\n#|\n\n|\Z)", re.DOTALL)
CAP_TOKEN_RE = re.compile(r"STARK_CAP_[A-Z0-9_]+")
# An app registration: `.caps_required = <expr>,` with the id/category nearby in the struct.
CAPS_REQ_RE = re.compile(r"\.caps_required\s*=\s*([^,;]+)")
ID_RE = re.compile(r'\.id\s*=\s*"([^"]+)"')
# A build-flag fork we forbid: #if / #ifdef / #if defined naming a feature or target.
IFDEF_RE = re.compile(r"^\s*#\s*if(?:def|ndef)?\b.*\b(WIFI|STM32|RP2350|LINUX)\b", re.MULTILINE)


def parse_caps(text):
    """(bits: name->shift, known: set of names in STARK_CAPS_KNOWN)."""
    bits = {}
    for name, shift in BIT_RE.findall(text):
        bits[name] = int(shift)
    m = KNOWN_RE.search(text)
    known = set(CAP_TOKEN_RE.findall(m.group(1))) if m else set()
    return bits, known


def iter_sources():
    for d in SRC_DIRS:
        for p in sorted((PROJECT_ROOT / d).rglob("*.c")):
            yield p
        for p in sorted((PROJECT_ROOT / d).rglob("*.h")):
            yield p


def app_matrix(bits):
    """List of (id, caps_required-expr, [token,...]) for every app registration found."""
    rows = []
    for p in sorted((PROJECT_ROOT / "apps").rglob("*.c")):
        text = p.read_text()
        for m in CAPS_REQ_RE.finditer(text):
            expr = m.group(1).strip()
            # the id declared in the same struct (nearest .id before this .caps_required)
            before = text[: m.start()]
            ids = ID_RE.findall(before)
            app_id = ids[-1] if ids else p.parent.name
            tokens = CAP_TOKEN_RE.findall(expr)
            rows.append((app_id, expr, tokens))
    return rows


def check():
    errors = []
    if not CAPS_H.exists():
        return ["%s not found" % CAPS_H]
    bits, known = parse_caps(CAPS_H.read_text())

    # 1. distinct single bits
    if not bits:
        errors.append("no STARK_CAP_* bits parsed from %s" % CAPS_H.name)
    shifts = {}
    for name, shift in bits.items():
        shifts.setdefault(shift, []).append(name)
    for shift, names in sorted(shifts.items()):
        if len(names) > 1:
            errors.append("bit %d is shared by %s" % (shift, ", ".join(sorted(names))))

    # 2. STARK_CAPS_KNOWN == the defined bits
    defined = set(bits)
    for missing in sorted(defined - known):
        errors.append("%s is defined but missing from STARK_CAPS_KNOWN" % missing)
    for extra in sorted(known - defined):
        errors.append("STARK_CAPS_KNOWN names %s, which is not a defined bit" % extra)

    # 3. every app's caps_required names defined bits only
    for app_id, expr, tokens in app_matrix(bits):
        if not tokens and expr.strip() != "0":
            errors.append("app '%s' has an unparsable caps_required: %s" % (app_id, expr))
        for tok in tokens:
            if tok not in defined:
                errors.append("app '%s' requires %s, which is not a defined bit" % (app_id, tok))

    # 4. no per-feature / per-target build-flag fork
    for p in iter_sources():
        for m in IFDEF_RE.finditer(p.read_text()):
            line = m.group(0).strip()
            errors.append("%s gates code on a feature/target macro: %s"
                          % (p.relative_to(PROJECT_ROOT), line))
    return errors


def main(argv=None):
    ap = argparse.ArgumentParser(description="Check the capability matrix and the no-build-flag rule.")
    ap.add_argument("--list", action="store_true", help="print the app -> capabilities matrix and exit")
    args = ap.parse_args(argv)

    bits, _ = parse_caps(CAPS_H.read_text()) if CAPS_H.exists() else ({}, set())
    if args.list:
        print("capabilities: " + ", ".join("%s(1<<%d)" % (n, s) for n, s in sorted(bits.items(), key=lambda x: x[1])))
        for app_id, expr, tokens in sorted(app_matrix(bits)):
            print("  %-12s %s" % (app_id, "—" if expr.strip() == "0" else " | ".join(tokens)))
        return 0

    errors = check()
    if errors:
        print("check_caps: %d finding(s)" % len(errors), file=sys.stderr)
        for e in errors:
            print("  - " + e, file=sys.stderr)
        return 1
    print("check_caps: capability matrix consistent, no feature/target build flags")
    return 0


if __name__ == "__main__":
    sys.exit(main())
