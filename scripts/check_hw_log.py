#!/usr/bin/env python3
"""
check_hw_log.py — replay an esp-emulator scenario by hand on the breadboard and
check the real serial log against the same rules (ROADMAP V1 exit 1, STARK-0805).

A production-image scenario (test/emu/*.toml, test/emu/soak/v01-soak.toml) is a
key sequence plus the log lines it must produce. On silicon the keys are the
operator's fingers and the log is a capture of the console, so this script

  * says which scenarios can be replayed on hardware at all, and why not;
  * turns one into numbered operator steps (keys, resets, waits);
  * checks a captured log with scripts/test_emu.py's own check_log — the same
    `expect` / `expect_re` order, `forbid` (plus the default panic list),
    `forbid_after`, `forbid_between`, `heap_min`, `heap_delta_max` and the
    drops=0 rule on every `diag:` line. Only the GDB schedule check is dropped,
    and the harness's own `--- phase N ---` markers are not expected.

Not replayable: the storage test image (fake transports, test/emu/storage/ and
most of soak/), esp-emu's RMT loopback (no carrier, no light: v1-ir.md instead),
the host-side UART1 peer (v1-labtools.md checks 28-29 instead), pokes, and
control-channel commands other than a chip reset.

The scenarios were written against what esp-emu models, so they pass on the
breadboard only in the matching setup, "bench E" (docs/bringup/v1-parts.md):
no microSD card; no PN532; no Sub-GHz module; nothing fitted to the IR pins;
slot 0's ID EEPROM (0x50) programmed with the `zeroes` image
(`gen_module_vectors.py --emit DIR zeroes`, the emulator's all-zero device);
nothing at 0x51 and nothing else on I2C. Scenarios that expect the defaults
need erased NVS first (`idf.py erase-flash`, then flash).

Usage:
  check_hw_log.py --list                  every scenario: replayable or why not
  check_hw_log.py --steps SCENARIO.toml   operator steps for one scenario
  check_hw_log.py SCENARIO.toml LOG       check a serial capture of that replay

Exit status: 0 pass (or --list / --steps printed), 1 the log fails,
2 usage error or a scenario that cannot be replayed on hardware.
"""
import argparse
import re
import sys
import tomllib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_emu as emu  # noqa: E402

ROOT = emu.ROOT
SCENARIO_GLOBS = ("test/emu/*.toml", "test/emu/soak/*.toml", "test/emu/storage/*.toml")
PHASE_MARK = re.compile(r"^--- phase \d+ ---$")
ANSI = re.compile(r"\x1b\[[0-9;]*m")
LONG_MS = 500  # stark_input long press (TESTING / v1-core check 16)
REPEAT_MS = 400  # first auto-repeat
DIAG_PERIOD_S = 10  # CONFIG_STARK_DIAG_LOG_PERIOD_S default


def load(path):
    scenario = tomllib.loads(Path(path).read_text())
    scenario.setdefault("name", Path(path).stem)
    return scenario


def not_replayable(scenario):
    """Reasons the scenario cannot be replayed on the breadboard (empty = it can)."""
    reasons = []
    if scenario.get("image", "production") != "production":
        reasons.append("needs the storage test image (fake transports)")
    if scenario.get("rmt_loopback"):
        reasons.append("esp-emu RMT loopback (baseband, no light): docs/bringup/v1-ir.md")
    if scenario.get("uart1_peer"):
        reasons.append("host-side UART1 peer: docs/bringup/v1-labtools.md checks 28-29")
    if scenario.get("pokes"):
        reasons.append("GDB pokes into the test medium")
    for c in scenario.get("control", []):
        if c["command"] != "reset":
            reasons.append("esp-emu control command %r" % c["command"])
    return reasons


def phase_count(scenario):
    return 1 + sum(1 for c in scenario.get("control", []) if c["command"] == "reset")


def press_text(keys, hold_ms):
    what = " and ".join(keys)
    if len(keys) > 1:
        return "press %s together (hold about %d ms)" % (what, hold_ms)
    if hold_ms >= LONG_MS:
        return "hold %s about %d ms (long press%s)" % (
            what, hold_ms, ", auto-repeats" if what in ("UP", "DOWN", "LEFT", "RIGHT") else "")
    if hold_ms >= REPEAT_MS:
        return "hold %s about %d ms" % (what, hold_ms)
    return "press %s" % what


def steps(scenario):
    """Operator steps, as a list of strings."""
    out = []
    if any("(defaults)" in e for e in scenario.get("expect", [])):
        out.append("Erase the flash first (idf.py -p PORT erase-flash, then flash): "
                   "the scenario expects the default settings.")
    out.append("Set up bench E (check_hw_log.py --help); capture the console from before "
               "the reset (idf.py -p PORT monitor | tee LOG).")
    out.append("Press RESET (EN) and wait for `boot: ui_ready`.")
    resets = sorted((c.get("phase", 0), c["at_ms"]) for c in scenario.get("control", [])
                    if c["command"] == "reset")
    for phase in range(phase_count(scenario)):
        presses = sorted((p for p in scenario.get("presses", []) if p.get("phase", 0) == phase),
                         key=lambda p: p["at_ms"])
        last_end = None
        i = 0
        while i < len(presses):
            p = presses[i]
            end = p["at_ms"] + p.get("hold_ms", 60)
            group = [p["key"].upper()]
            # presses that overlap this one are one chord
            while i + 1 < len(presses) and presses[i + 1]["at_ms"] < end:
                i += 1
                group.append(presses[i]["key"].upper())
                end = max(end, presses[i]["at_ms"] + presses[i].get("hold_ms", 60))
            if last_end is not None and p["at_ms"] - last_end >= 1000:
                out.append("Wait about %.0f s." % ((p["at_ms"] - last_end) / 1000.0))
            text = press_text(group, end - p["at_ms"])
            out.append(text[0].upper() + text[1:] + ".")
            last_end = end
            i += 1
        reset_at = next((at for ph, at in resets if ph == phase), None)
        if reset_at is not None:
            if last_end is not None and reset_at - last_end >= 1000:
                out.append("Wait about %.0f s." % ((reset_at - last_end) / 1000.0))
            out.append("Press RESET (EN) and wait for `boot: ui_ready`.")
    wait = DIAG_PERIOD_S + 2
    if "heap_delta_max" in scenario:
        wait = max(wait, (scenario["end_ms"] - (last_end or 0)) // 1000 + 2)
    out.append("Wait %d s with no key pressed (the next `diag:` line), then stop the capture."
               % wait)
    return out


def hardware_expectations(scenario):
    """The scenario with the emulator harness's phase markers removed."""
    s = dict(scenario)
    s["expect"] = [e for e in scenario.get("expect", []) if not PHASE_MARK.match(e)]
    return s


def check(scenario, log):
    clean = ANSI.sub("", log.replace("\r\n", "\n").replace("\r", "\n"))
    return emu.check_log(hardware_expectations(scenario), clean, None)


def all_scenarios():
    files = []
    for pattern in SCENARIO_GLOBS:
        files.extend(sorted(ROOT.glob(pattern)))
    return files


def main(argv=None):
    ap = argparse.ArgumentParser(description="Replay an emulator scenario on hardware and "
                                 "check the serial log.")
    ap.add_argument("--list", action="store_true", help="list every scenario")
    ap.add_argument("--steps", action="store_true", help="print the operator steps")
    ap.add_argument("files", nargs="*", help="SCENARIO.toml [LOG]")
    args = ap.parse_args(argv)

    if args.list:
        if args.files or args.steps:
            ap.error("--list takes no other argument")
        for f in all_scenarios():
            s = load(f)
            why = not_replayable(s)
            print("%-34s %s" % (f.relative_to(ROOT), "replayable" if not why else
                                "not replayable: " + "; ".join(why)))
        return 0
    want = 1 if args.steps else 2
    if len(args.files) != want:
        ap.error("--steps takes SCENARIO.toml" if args.steps else "expected SCENARIO.toml LOG")
    scenario = load(args.files[0])
    why = not_replayable(scenario)
    if why:
        print("ERROR: %s cannot be replayed on hardware: %s" % (scenario["name"], "; ".join(why)),
              file=sys.stderr)
        return 2
    if args.steps:
        print("%s (%s)" % (scenario["name"], args.files[0]))
        for n, text in enumerate(steps(scenario), 1):
            print("%2d. %s" % (n, text))
        return 0
    failures = check(scenario, Path(args.files[1]).read_text(errors="replace"))
    for f in failures:
        print("FAIL: %s — %s" % (scenario["name"], f), file=sys.stderr)
    if failures:
        return 1
    print("PASS: %s (hardware log %s)" % (scenario["name"], args.files[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
