#!/usr/bin/env python3
"""
test_emu.py — run the production firmware in Espressif's esp-emulator and
check a scenario against its serial log (Tier 3, docs/VALIDATION.md).

The firmware is the exact artifact CI builds (build/merged-binary.bin): no
test build, no simulator conditional. Two things come from outside it:

  * Key presses. esp-emu models the ESP32-S3's GPIO controller but offers no
    way to drive an external pad, so a scenario's presses are injected with
    GDB (esp-emu's documented breakpoint + `set var` + continue technique) at
    the input service's sampling boundary: a breakpoint inside
    input_core_update() rewrites its `raw_bitmap` argument while `now_ms`
    (emulated milliseconds) lies in a press window. Everything downstream —
    debounce/long/repeat core, event publishing, `key:` logs, event bus, UI,
    menu, app lifecycle, rendering over emulated GP-SPI/GDMA — is the
    production code. The pad -> bitmap step is host-tested instead
    (test/host/test_input_sample.c); electrical wiring is a HIL check.
  * Time. The GDB loop runs until `now_ms` reaches the scenario's `end_ms`,
    so schedules are in emulated time and independent of host speed.

Scenario files (test/emu/*.toml):

  name = "emu-v0-boot-and-menu"
  end_ms = 3500                         # emulated ms to run
  presses = [ { key = "DOWN", at_ms = 400, hold_ms = 60 } ]
  expect = [ "boot: ui_ready in ", 'menu: sel=1 "Buzzer Test"' ]  # in order
  forbid = [ "panic:" ]                 # optional; added to DEFAULT_FORBID
  heap_min = 200000                     # optional; first `diag: heap=` gate

Usage:
  test_emu.py [--emu PATH] [--gdb PATH] [--firmware BIN] [--elf ELF]
              [--log-dir DIR] [scenario.toml ...]     (default: test/emu/*.toml)

Exit status: 0 when every scenario passes, 1 otherwise, 2 on a setup error.
"""
import argparse
import glob
import os
import re
import shutil
import socket
import subprocess
import sys
import time
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KEYS = ("UP", "DOWN", "LEFT", "RIGHT", "OK", "BACK")  # stark_key_t order = bit order
DEFAULT_FORBID = ("panic:", "Guru Meditation", "ui: render failed", "abort() was called",
                  "Backtrace:")
END_MARK = "STARK-EMU-END"
INJECT_SOURCE = ROOT / "components/stark_input/input_core.c"
WALL_LIMIT_S = 600


def inject_line():
    """First statement line of input_core_update(): after the windowed
    `entry`, where `raw_bitmap` and `now_ms` are live and writable."""
    lines = INJECT_SOURCE.read_text().splitlines()
    for i, line in enumerate(lines):
        if re.match(r"^size_t input_core_update\(", line):
            for j in range(i, len(lines)):
                if lines[j].strip() == "{":
                    for k in range(j + 1, len(lines)):
                        if lines[k].strip():
                            return k + 1  # 1-based
    raise SystemExit("cannot find input_core_update() in %s" % INJECT_SOURCE)


def gdb_script(scenario, port):
    out = [
        "set pagination off",
        "set confirm off",
        "set height 0",
        "target remote 127.0.0.1:%d" % port,
        "break input_core.c:%d" % inject_line(),
        "continue",
        "while now_ms < %d" % int(scenario["end_ms"]),
    ]
    for p in scenario.get("presses", []):
        key = p["key"].upper()
        if key not in KEYS:
            raise SystemExit("%s: unknown key %r" % (scenario["name"], key))
        start = int(p["at_ms"])
        end = start + int(p.get("hold_ms", 60))
        out += [
            "  if now_ms >= %d && now_ms < %d" % (start, end),
            "    set var raw_bitmap = raw_bitmap | %d" % (1 << KEYS.index(key)),
            "  end",
        ]
    out += [
        "  continue",
        "end",
        'printf "%s %%u\\n", now_ms' % END_MARK,
        "delete",
        "detach",
        "quit",
    ]
    return "\n".join(out) + "\n"


def check_log(scenario, log, gdb_log):
    """List of failure messages (empty = pass)."""
    failures = []
    if END_MARK not in gdb_log:
        failures.append("GDB schedule did not reach end_ms=%s (gdb log tail: %s)"
                        % (scenario["end_ms"], gdb_log.strip().splitlines()[-3:]))
    lines = log.splitlines()
    pos = 0
    for want in scenario.get("expect", []):
        for i in range(pos, len(lines)):
            if want in lines[i]:
                pos = i + 1
                break
        else:
            failures.append("expected, in order, but not found: %r" % want)
            break
    for bad in tuple(scenario.get("forbid", ())) + DEFAULT_FORBID:
        hit = next((line for line in lines if bad in line), None)
        if hit is not None:
            failures.append("forbidden output %r: %s" % (bad, hit.strip()))
    if "heap_min" in scenario:
        m = re.search(r"diag: heap=(\d+)", log)
        if not m:
            failures.append("no `diag: heap=` line")
        elif int(m.group(1)) < int(scenario["heap_min"]):
            failures.append("diag: heap=%s is below %s" % (m.group(1), scenario["heap_min"]))
        else:
            print("    diag: heap=%s (>= %s)" % (m.group(1), scenario["heap_min"]))
    return failures


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def run_scenario(path, args):
    scenario = tomllib.loads(Path(path).read_text())
    name = scenario.get("name") or Path(path).stem
    scenario["name"] = name
    log_path = Path(args.log_dir) / (name + ".log")
    gdb_path = Path(args.log_dir) / (name + ".gdb.log")
    script_path = Path(args.log_dir) / (name + ".gdb")
    port = free_port()
    script_path.write_text(gdb_script(scenario, port))
    print("=== %s" % name, flush=True)
    started = time.monotonic()
    with open(log_path, "w") as log_f:
        emu = subprocess.Popen(
            [args.emu, "--chip", "esp32s3", "--firmware", args.firmware,
             "--log-color", "never", "--gdb", str(port), "--gdb-halt",
             "--timeout", "%ds" % WALL_LIMIT_S],
            stdout=log_f, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        try:
            time.sleep(0.5)
            with open(gdb_path, "w") as gdb_f:
                subprocess.run([args.gdb, "-q", "-batch", "-x", str(script_path), args.elf],
                               stdout=gdb_f, stderr=subprocess.STDOUT,
                               timeout=WALL_LIMIT_S, check=False)
            time.sleep(1.0)  # let the firmware run on briefly: late panics still count
        except subprocess.TimeoutExpired:
            pass
        finally:
            emu.terminate()
            try:
                emu.wait(timeout=10)
            except subprocess.TimeoutExpired:
                emu.kill()
    log = log_path.read_text(errors="replace")
    failures = check_log(scenario, log, gdb_path.read_text(errors="replace"))
    for f in failures:
        print("FAIL: %s — %s (serial log: %s)" % (name, f, log_path), file=sys.stderr)
    if not failures:
        print("PASS: %s (%.0f s)" % (name, time.monotonic() - started))
    return not failures


def find_gdb():
    for cand in [os.environ.get("STARK_GDB"), shutil.which("xtensa-esp32s3-elf-gdb")] + \
            sorted(glob.glob("/opt/esp/tools/xtensa-esp-elf-gdb/*/xtensa-esp-elf-gdb/bin/"
                             "xtensa-esp32s3-elf-gdb")):
        if cand and os.access(cand, os.X_OK):
            return cand
    return None


def main(argv=None):
    ap = argparse.ArgumentParser(description="Run esp-emulator scenarios.")
    ap.add_argument("--emu", default=os.environ.get("STARK_ESP_EMU"))
    ap.add_argument("--gdb", default=None)
    ap.add_argument("--firmware", default=str(ROOT / "build/merged-binary.bin"))
    ap.add_argument("--elf", default=str(ROOT / "build/stark-one.elf"))
    ap.add_argument("--log-dir", default=str(ROOT / "build/emu"))
    ap.add_argument("scenarios", nargs="*")
    args = ap.parse_args(argv)

    args.gdb = args.gdb or find_gdb()
    problems = [msg for ok, msg in (
        (args.emu and os.access(args.emu, os.X_OK), "esp-emu not found (--emu / STARK_ESP_EMU)"),
        (args.gdb, "xtensa-esp32s3-elf-gdb not found (--gdb / STARK_GDB)"),
        (Path(args.firmware).is_file(), "no %s — build and merge-bin first" % args.firmware),
        (Path(args.elf).is_file(), "no %s — build first" % args.elf),
    ) if not ok]
    for p in problems:
        print("ERROR: " + p, file=sys.stderr)
    if problems:
        return 2

    scenarios = args.scenarios or sorted(glob.glob(str(ROOT / "test/emu/*.toml")))
    if not scenarios:
        print("ERROR: no scenarios", file=sys.stderr)
        return 2
    Path(args.log_dir).mkdir(parents=True, exist_ok=True)
    results = [run_scenario(s, args) for s in scenarios]
    print("Emulator scenarios: %d passed, %d failed." % (results.count(True),
                                                        results.count(False)))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
