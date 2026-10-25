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
  expect_re = [ 'wtest ok .* frames=[1-9]' ]
                                        # optional; regular expressions, in order,
                                        # after the `expect` lines — for counts
                                        # that must be non-zero, never durations
  forbid = [ "panic:" ]                 # optional; added to DEFAULT_FORBID
  forbid_after = [ { after = "ui: chord reserved", text = "app: start" } ]
                                        # optional; text must not follow the
                                        # first line containing `after`
  forbid_between = [ { after = "storage: ejected", before = "mount requested",
                       text = "storage: mounted" } ]
                                        # optional; text must not appear between
                                        # the first `after` line and the next
                                        # `before` line
  heap_min = 200000                     # optional; first `diag: heap=` gate
  heap_delta_max = 1024                 # optional; first vs last stark_diag heap
  wall_limit_s = 1500                   # optional; host wall-clock cap (default 600)
  control = [ { at_ms = 2500, command = "reset" } ]
                                        # optional. "reset" ends the current phase
                                        # at at_ms and reboots: the next phase is
                                        # a new esp-emu run over the same flash
                                        # image (saved with --save-state, so NVS
                                        # survives; the 0.44.0 GDB stub does not
                                        # survive an in-process chip reset).
                                        # Other commands ("erase-region <off>
                                        # <len>") go to the esp-emu control
                                        # channel at at_ms. end_ms applies to the
                                        # last phase; presses / pokes / control
                                        # take an optional `phase` (default 0).
  rmt_loopback = "0:2"                  # optional; esp-emu --rmt-loopback TX:RX — the
                                        # first RMT TX channel's baseband into the
                                        # first RX channel (verified pair on
                                        # esp32s3; ADR-0019 §2: no carrier, no light)
  image = "storage"                     # optional; the firmware it needs (below)
  pokes = [ { at_ms = 2500, var = "emu_medium_present", value = 0 } ]
                                        # optional, storage image only: a global
                                        # of the test medium set over GDB once
                                        # emulated time reaches at_ms
  (always: every `diag:` line carrying drops= must read drops=0)

Images (ADR-0018): "production" (default) is build/merged-binary.bin;
"storage" is the emulator storage test image build/emu-storage/ — the same
firmware with the storage medium replaced by internal-flash FAT, whose
scenarios live in test/emu/storage/. A scenario run against the other image
fails.

Usage:
  test_emu.py [--image production|storage] [--emu PATH] [--gdb PATH]
              [--firmware BIN] [--elf ELF] [--log-dir DIR] [scenario.toml ...]
              (default: test/emu/*.toml, or test/emu/storage/*.toml)

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
IMAGES = {
    "production": ("build/merged-binary.bin", "build/stark-one.elf", "test/emu/*.toml"),
    "storage": ("build/emu-storage/merged-binary.bin",
                "build/emu-storage/stark-one-emu-storage.elf", "test/emu/storage/*.toml"),
}


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


POKE_VARS = ("emu_medium_present", "emu_medium_formatted",  # the test medium's switches
             "emu_nfc_tag", "emu_nfc_fault")  # the fake PN532 transport's (ADR-0020 §3)


def control_shell(port, command):
    """A GDB `shell` line that sends one command to esp-emu's control channel.
    Fire and forget: GDB holds the CPU at the breakpoint while the shell runs,
    so the emulator only reads (and answers) the command once it resumes."""
    return ("shell python3 -c \"import socket; s = socket.create_connection(('127.0.0.1', %d)); "
            "s.sendall(b'%s\\n'); s.close()\"" % (port, command))


def gdb_script(scenario, port, control_port=0):
    """The GDB schedule for one phase (see split_phases)."""
    pokes = scenario.get("pokes", [])
    control = [c for c in scenario.get("control", []) if c["command"] != "reset"]
    if pokes and scenario.get("image") != "storage":
        raise SystemExit("%s: pokes need image = \"storage\"" % scenario["name"])
    out = [
        "set pagination off",
        "set confirm off",
        "set height 0",
        "target remote 127.0.0.1:%d" % port,
        "break input_core.c:%d" % inject_line(),
        "continue",
    ]
    # One GDB convenience variable per poke: it fires once, at the first
    # breakpoint hit at or after at_ms.
    out += ["set $poke%d = 0" % i for i in range(len(pokes))]
    out += ["set $ctl%d = 0" % i for i in range(len(control))]
    out.append("while now_ms < %d" % int(scenario["end_ms"]))
    for i, c in enumerate(control):
        out += [
            "  if $ctl%d == 0 && now_ms >= %d" % (i, int(c["at_ms"])),
            "    " + control_shell(control_port, c["command"]),
            "    set $ctl%d = 1" % i,
            "  end",
        ]
    for i, poke in enumerate(pokes):
        if poke["var"] not in POKE_VARS:
            raise SystemExit("%s: unknown poke variable %r" % (scenario["name"], poke["var"]))
        out += [
            "  if $poke%d == 0 && now_ms >= %d" % (i, int(poke["at_ms"])),
            "    set var %s = %d" % (poke["var"], int(poke["value"])),
            "    set $poke%d = 1" % i,
            "  end",
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


def split_phases(scenario):
    """One scenario dict per phase: phase k ends at the at_ms of the k-th
    "reset" (the last at end_ms) and holds the presses, pokes and other
    control commands whose `phase` is k."""
    resets = sorted((int(c.get("phase", 0)), int(c["at_ms"]))
                    for c in scenario.get("control", []) if c["command"] == "reset")
    for k, (phase, _) in enumerate(resets):
        if phase != k:
            raise SystemExit("%s: reset %d must be in phase %d" % (scenario["name"], k, k))
    phases = []
    for k in range(len(resets) + 1):
        one = {key: v for key, v in scenario.items() if key not in ("presses", "pokes", "control")}
        one["end_ms"] = resets[k][1] if k < len(resets) else int(scenario["end_ms"])
        for key in ("presses", "pokes", "control"):
            one[key] = [x for x in scenario.get(key, [])
                        if int(x.get("phase", 0)) == k and x.get("command") != "reset"]
        phases.append(one)
    return phases


def emu_command(args, scenario, firmware, port, control_port, wall, save_state):
    """The esp-emu command line for one phase."""
    cmd = [args.emu, "--chip", "esp32s3", "--firmware", firmware,
           "--log-color", "never", "--gdb", str(port), "--gdb-halt",
           "--control-tcp", "127.0.0.1:%d" % control_port, "--timeout", "%ds" % wall]
    loop = scenario.get("rmt_loopback")
    if loop is not None:
        if not re.match(r"^\d+:\d+$", str(loop)):
            raise SystemExit("%s: rmt_loopback must be \"TX:RX\"" % scenario["name"])
        cmd += ["--rmt-loopback", str(loop)]
    if save_state:
        cmd.append("--save-state")
    return cmd


def image_mismatch(scenario, image):
    """Failure message when a scenario needs another firmware image, else None."""
    want = scenario.get("image", "production")
    if want not in IMAGES:
        return "unknown image %r" % want
    if want != image:
        return "scenario needs the %s image, runner has the %s image" % (want, image)
    return None


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
    for want in scenario.get("expect_re", []):
        rx = re.compile(want)
        for i in range(pos, len(lines)):
            if rx.search(lines[i]):
                pos = i + 1
                break
        else:
            failures.append("expected regex, in order, but not matched: %r" % want)
            break
    for bad in tuple(scenario.get("forbid", ())) + DEFAULT_FORBID:
        hit = next((line for line in lines if bad in line), None)
        if hit is not None:
            failures.append("forbidden output %r: %s" % (bad, hit.strip()))
    # Every stark_diag line reports drops=0 (ROADMAP V0.1 exit 3, STARK-0109).
    for line in lines:
        m = re.search(r"diag: .*\bdrops=(\d+)", line)
        if m and int(m.group(1)) != 0:
            failures.append("event drops reported: %s" % line.strip())
    for rule in scenario.get("forbid_after", []):
        anchor = next((i for i, line in enumerate(lines) if rule["after"] in line), None)
        if anchor is None:
            failures.append("forbid_after anchor not found: %r" % rule["after"])
            continue
        hit = next((line for line in lines[anchor + 1:] if rule["text"] in line), None)
        if hit is not None:
            failures.append("%r after %r: %s" % (rule["text"], rule["after"], hit.strip()))
    for rule in scenario.get("forbid_between", []):
        start = next((i for i, line in enumerate(lines) if rule["after"] in line), None)
        if start is None:
            failures.append("forbid_between anchor not found: %r" % rule["after"])
            continue
        end = next((i for i in range(start + 1, len(lines)) if rule["before"] in lines[i]), None)
        if end is None:
            failures.append("forbid_between end not found: %r" % rule["before"])
            continue
        hit = next((line for line in lines[start + 1:end] if rule["text"] in line), None)
        if hit is not None:
            failures.append("%r between %r and %r: %s" % (rule["text"], rule["after"],
                                                         rule["before"], hit.strip()))
    if "heap_delta_max" in scenario:
        # stark_diag's periodic lines (they carry min=), not main's boot line
        heaps = [int(m.group(1)) for m in re.finditer(r"diag: heap=(\d+) min=", log)]
        if len(heaps) < 2:
            failures.append("heap_delta_max needs two stark_diag lines, found %d" % len(heaps))
        else:
            delta = abs(heaps[-1] - heaps[0])
            if delta > int(scenario["heap_delta_max"]):
                failures.append("heap moved %d B (%d -> %d), more than %s" % (
                    delta, heaps[0], heaps[-1], scenario["heap_delta_max"]))
            else:
                print("    heap %d -> %d B (delta %d <= %s)" % (
                    heaps[0], heaps[-1], delta, scenario["heap_delta_max"]))
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
    mismatch = image_mismatch(scenario, args.image)
    if mismatch:
        print("FAIL: %s — %s" % (name, mismatch), file=sys.stderr)
        return False
    log_path = Path(args.log_dir) / (name + ".log")
    gdb_path = Path(args.log_dir) / (name + ".gdb.log")
    wall = int(scenario.get("wall_limit_s", WALL_LIMIT_S))
    phases = split_phases(scenario)
    firmware = args.firmware
    if len(phases) > 1:
        # One flash image for every phase: esp-emu --save-state writes the
        # flash (NVS included) back into it when the run ends.
        firmware = str(Path(args.log_dir) / (name + ".flash.bin"))
        shutil.copyfile(args.firmware, firmware)
    print("=== %s" % name, flush=True)
    started = time.monotonic()
    log_path.write_text("")
    gdb_path.write_text("")
    for k, phase in enumerate(phases):
        port = free_port()
        control_port = free_port()
        script_path = Path(args.log_dir) / ("%s.%d.gdb" % (name, k))
        script_path.write_text(gdb_script(phase, port, control_port))
        cmd = emu_command(args, scenario, firmware, port, control_port, wall, len(phases) > 1)
        with open(log_path, "a") as log_f, open(gdb_path, "a") as gdb_f:
            log_f.write("--- phase %d ---\n" % k)
            log_f.flush()
            emu = subprocess.Popen(cmd, stdout=log_f, stderr=subprocess.STDOUT,
                                   stdin=subprocess.DEVNULL)
            try:
                time.sleep(0.5)
                subprocess.run([args.gdb, "-q", "-batch", "-x", str(script_path), args.elf],
                               stdout=gdb_f, stderr=subprocess.STDOUT,
                               timeout=wall, check=False)
                time.sleep(1.0)  # let the firmware run on briefly: late panics still count
            except subprocess.TimeoutExpired:
                pass
            finally:
                emu.terminate()  # --save-state writes the flash back on exit
                try:
                    emu.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    emu.kill()
    log = log_path.read_text(errors="replace")
    gdb_log = gdb_path.read_text(errors="replace")
    failures = check_log(scenario, log, gdb_log)
    if len(phases) > 1 and gdb_log.count(END_MARK) != len(phases):
        failures.append("only %d of %d phases reached their end" % (gdb_log.count(END_MARK),
                                                                     len(phases)))
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
    ap.add_argument("--image", choices=sorted(IMAGES), default="production")
    ap.add_argument("--emu", default=os.environ.get("STARK_ESP_EMU"))
    ap.add_argument("--gdb", default=None)
    ap.add_argument("--firmware", default=None)
    ap.add_argument("--elf", default=None)
    ap.add_argument("--log-dir", default=str(ROOT / "build/emu"))
    ap.add_argument("scenarios", nargs="*")
    args = ap.parse_args(argv)
    firmware, elf, default_glob = IMAGES[args.image]
    args.firmware = args.firmware or str(ROOT / firmware)
    args.elf = args.elf or str(ROOT / elf)

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

    scenarios = args.scenarios or sorted(glob.glob(str(ROOT / default_glob)))
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
