#!/usr/bin/env python3
"""
bringup_figures.py — the figures a console capture carries, for ROADMAP V1 exit 6
(docs/bringup/v1-core.md check 34, docs/measurements.md "Hardware"). STARK-0807.

Reads one or more serial captures (idf.py monitor output, CR and ANSI colours
ignored; several boots per file allowed) and reports, per boot and over all of
them, what the firmware itself logs:

  boot: ui_ready in <n> ms           -> worst / median over the boots (target <= 600 ms)
  display: full refresh <n> ms       -> worst / median (target <= 60 ms)
  diag: heap=<n>  (boot line)        -> lowest root heap over the boots (gate >= 200 kB)
  diag: heap= min= fps= drops= overruns=  (stark_diag's periodic line)
                                     -> fps range; drops (gate 0); overruns added
                                        during a boot (gate 0 over a 60 s session);
                                        heap first -> last periodic line in a boot
  displaytest: fps=<n>               -> the panel's ceiling at the chosen clock

These are the firmware's own figures. v1-core.md also asks for analyser figures
the log cannot give (first pixel, key-to-pixel latency, ui_ready from the EN
edge): they are listed as such and never filled in from a log.

The root-menu FPS gate (>= 20) depends on what was on screen, so the fps range
is reported without a verdict: v1-core check 18 says which capture to read.

--compare LOG... adds a column with the same figures from esp-emulator logs
(CI's `stark-one-emu-logs` artifact), for the "alongside the simulated figures"
rule of V1 exit 6.

Usage:
  bringup_figures.py LOG [LOG ...] [--compare EMU_LOG ...]

Exit status: 0 every gated figure passes, 1 one fails, 2 no boot found.
"""
import argparse
import re
import statistics
import sys
from pathlib import Path

ANSI = re.compile(r"\x1b\[[0-9;]*m")
BOOT = re.compile(r"\bboot: stark-one ")
UI_READY = re.compile(r"\bboot: ui_ready in (\d+) ms")
REFRESH = re.compile(r"\bdisplay: full refresh (\d+) ms")
DIAG_BOOT = re.compile(r"\bdiag: heap=(\d+)\s*$")
DIAG = re.compile(r"\bdiag: heap=(\d+) min=(\d+) fps=(\d+)\.(\d) drops=(\d+) overruns=(\d+)")
DISPLAYTEST = re.compile(r"\bdisplaytest: fps=(\d+)\.(\d)")

UI_READY_MAX_MS = 600
REFRESH_MAX_MS = 60
HEAP_MIN_B = 200000

ANALYSER_ONLY = (
    ("First pixel from reset", "<= 400 ms", "v1-core check 5"),
    ("Key-to-pixel latency, selection move", "<= 50 ms", "v1-core check 17"),
    ("`boot: ui_ready` from the EN edge", "<= 600 ms", "v1-core check 4"),
)


def clean(text):
    return ANSI.sub("", text.replace("\r\n", "\n").replace("\r", "\n"))


def split_boots(text):
    """Lists of lines, one per boot; lines before the first boot line are dropped."""
    boots = []
    for line in clean(text).splitlines():
        if BOOT.search(line):
            boots.append([])
        if boots:
            boots[-1].append(line)
    return boots


def boot_figures(lines):
    f = {"ui_ready": None, "refresh": [], "root_heap": None, "diag": [], "displaytest": []}
    for line in lines:
        m = UI_READY.search(line)
        if m and f["ui_ready"] is None:
            f["ui_ready"] = int(m.group(1))
        m = REFRESH.search(line)
        if m:
            f["refresh"].append(int(m.group(1)))
        m = DIAG_BOOT.search(line)
        if m and f["root_heap"] is None:
            f["root_heap"] = int(m.group(1))
        m = DIAG.search(line)
        if m:
            f["diag"].append({"heap": int(m.group(1)), "min": int(m.group(2)),
                              "fps": int(m.group(3)) + int(m.group(4)) / 10.0,
                              "drops": int(m.group(5)), "overruns": int(m.group(6))})
        m = DISPLAYTEST.search(line)
        if m:
            f["displaytest"].append(int(m.group(1)) + int(m.group(2)) / 10.0)
    return f


def summarise(texts):
    """Figures over every boot in the given capture texts (None when there is no boot)."""
    boots = [boot_figures(b) for t in texts for b in split_boots(t)]
    if not boots:
        return None
    ui = [b["ui_ready"] for b in boots if b["ui_ready"] is not None]
    refresh = [ms for b in boots for ms in b["refresh"]]
    heaps = [b["root_heap"] for b in boots if b["root_heap"] is not None]
    diag = [d for b in boots for d in b["diag"]]
    overruns = [b["diag"][-1]["overruns"] - b["diag"][0]["overruns"] for b in boots
                if len(b["diag"]) >= 2]
    deltas = [b["diag"][-1]["heap"] - b["diag"][0]["heap"] for b in boots if len(b["diag"]) >= 2]
    return {
        "boots": len(boots),
        "ui_ready": ui,
        "refresh": refresh,
        "root_heap": heaps,
        "fps": [d["fps"] for d in diag],
        "drops": sum(d["drops"] for d in diag),
        "diag_lines": len(diag),
        "overruns": overruns,
        "heap_delta": deltas,
        "heap_min": min((d["min"] for d in diag), default=None),
        "displaytest": [fps for b in boots for fps in b["displaytest"]],
    }


def spread(values, unit):
    if not values:
        return "—"
    if len(values) == 1:
        return "%s %s" % (fmt(values[0]), unit)
    return "worst %s, median %s %s (n=%d)" % (fmt(max(values)), fmt(statistics.median(values)),
                                              unit, len(values))


def fmt(v):
    return ("%.1f" % v).rstrip("0").rstrip(".") if isinstance(v, float) else "%d" % v


def heap_deltas(deltas):
    if not deltas:
        return "—"
    if len(deltas) <= 4:
        return "%s B (n=%d)" % (", ".join("%+d" % d for d in deltas), len(deltas))
    return "largest change %+d B (n=%d)" % (max(deltas, key=abs), len(deltas))


def rows(s):
    """(metric, target, value, verdict) for one summary; verdict None = not gated."""
    out = []

    def gate(ok):
        return None if ok is None else ("pass" if ok else "FAIL")

    out.append(("`boot: ui_ready` (logged)", "<= %d ms" % UI_READY_MAX_MS,
                spread(s["ui_ready"], "ms"),
                gate(max(s["ui_ready"]) <= UI_READY_MAX_MS if s["ui_ready"] else None)))
    out.append(("Full-screen refresh (`display: full refresh`)", "<= %d ms" % REFRESH_MAX_MS,
                spread(s["refresh"], "ms"),
                gate(max(s["refresh"]) <= REFRESH_MAX_MS if s["refresh"] else None)))
    out.append(("Free internal heap at the root menu (boot `diag: heap=`)",
                ">= %d B" % HEAP_MIN_B,
                "lowest %d B (n=%d)" % (min(s["root_heap"]), len(s["root_heap"]))
                if s["root_heap"] else "—",
                gate(min(s["root_heap"]) >= HEAP_MIN_B if s["root_heap"] else None)))
    out.append(("`diag:` fps", "(record; root menu >= 20 while scrolling, v1-core check 18)",
                "%s to %s (n=%d)" % (fmt(min(s["fps"])), fmt(max(s["fps"])), len(s["fps"]))
                if s["fps"] else "—", None))
    out.append(("Dropped events (`diag:` drops)", "0",
                "%d over %d lines" % (s["drops"], s["diag_lines"]) if s["diag_lines"] else "—",
                gate(s["drops"] == 0 if s["diag_lines"] else None)))
    out.append(("Frame overruns added during a boot", "0",
                "worst %d (n=%d)" % (max(s["overruns"]), len(s["overruns"]))
                if s["overruns"] else "—",
                gate(max(s["overruns"]) == 0 if s["overruns"] else None)))
    out.append(("Heap, first to last `diag:` line in a boot", "(record; within 1 kB over an "
                "hour, v1-core check 33)",
                heap_deltas(s["heap_delta"]), None))
    out.append(("Lowest `min=` (heap low-water mark)", "(record)",
                "%d B" % s["heap_min"] if s["heap_min"] is not None else "—", None))
    out.append(("Display Test (`displaytest: fps=`)", "(record: the panel's ceiling)",
                spread(s["displaytest"], "fps"), None))
    return out


def report(hw, sim=None):
    lines = ["Boots read: %d%s" % (hw["boots"], "" if sim is None else
                                    " (simulated: %d)" % sim["boots"]), ""]
    head = "| Metric | Target | Hardware | Verdict |" + ("" if sim is None else " Simulated |")
    lines += [head, "|" + " --- |" * (4 if sim is None else 5)]
    sim_rows = rows(sim) if sim is not None else None
    for i, (metric, target, value, verdict) in enumerate(rows(hw)):
        cell = "| %s | %s | %s | %s |" % (metric, target, value, verdict or "—")
        if sim_rows is not None:
            cell += " %s |" % sim_rows[i][2]
        lines.append(cell)
    lines += ["", "Not in the log (logic analyser, docs/bringup/v1-core.md):"]
    lines += ["* %s (%s): %s" % a for a in ANALYSER_ONLY]
    failed = any(r[3] == "FAIL" for r in rows(hw))
    return "\n".join(lines), failed


def main(argv=None):
    ap = argparse.ArgumentParser(description="Figures from bring-up console captures.")
    ap.add_argument("logs", nargs="+", help="serial captures from the breadboard")
    ap.add_argument("--compare", nargs="+", metavar="EMU_LOG",
                    help="esp-emulator logs for the simulated column")
    args = ap.parse_args(argv)
    hw = summarise(Path(p).read_text(errors="replace") for p in args.logs)
    if hw is None:
        print("ERROR: no `boot: stark-one` line in %s" % ", ".join(args.logs), file=sys.stderr)
        return 2
    sim = None
    if args.compare:
        sim = summarise(Path(p).read_text(errors="replace") for p in args.compare)
        if sim is None:
            print("ERROR: no boot in the --compare logs", file=sys.stderr)
            return 2
    text, failed = report(hw, sim)
    print(text)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
