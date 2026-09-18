#!/usr/bin/env python3
"""
scripts/check_pins.py — cross-check components/stark_board/board_devkitc1.c
against diagram.json.

board_devkitc1.c is the single source of truth for pin numbers (AGENTS.md).
diagram.json is expected to lag behind it — most V0 signals (display,
buttons, buzzer, SD, I2C) are not wired into the simulator yet, and that is
fine (each is added by the task that brings the corresponding hardware up).

What this script actually checks: for every signal the board map assigns a
real GPIO to, if diagram.json *also* wires that same GPIO number, the two
must agree on what the pin is for (a Wokwi part whose id/type looks like
the signal it is supposed to be). A signal missing from the diagram is
reported, not failed. A signal wired to the *wrong kind of part* is failed.

This does not duplicate stark_board_validate()'s pin-collision / reserved-
range checks (that logic has one home: components/stark_board/stark_board.c,
exercised by test/host/test_board.c) — this script only checks agreement
between the two committed sources of pin data.

This is not a second pin map. Every GPIO *number* used below is parsed out
of board_devkitc1.c at run time (parse_board_pins()) — none is hardcoded
here. EXPECTED_HINTS below maps a signal *name* to the kind of Wokwi part
it should be wired to (from WOKWI.md's documented parts table), which is a
much smaller, slower-changing fact than a pin number and encodes no pin
value itself. If a field in stark_board_pins_t is ever renamed, the worst
case is that this script silently stops checking that one signal's Wokwi
part (it still reports the pin as present, just without the part-type
check) — it will not report a wrong pin number, because it never invents
one.

Usage: python3 scripts/check_pins.py
Exit code 0 on success (or tolerated omissions), 1 on a genuine mismatch.
"""
import json
import re
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
BOARD_C = PROJECT_ROOT / "components" / "stark_board" / "board_devkitc1.c"
DIAGRAM_JSON = PROJECT_ROOT / "diagram.json"

# Expected Wokwi part-id/type substrings for each board signal, per
# WOKWI.md §4's documented parts table. A signal not listed here (sd_cs,
# i2c_sda, i2c_scl — no Wokwi part exists for them yet) is only checked for
# presence, never for a specific part match.
EXPECTED_HINTS = {
    "sclk": ["lcd"],
    "mosi": ["lcd"],
    "miso": ["lcd"],
    "tft_cs": ["lcd"],
    "tft_dc": ["lcd"],
    "tft_rst": ["lcd"],
    "tft_bl": ["lcd"],
    "key[0]": ["btn_up"],
    "key[1]": ["btn_down"],
    "key[2]": ["btn_left"],
    "key[3]": ["btn_right"],
    "key[4]": ["btn_ok"],
    "key[5]": ["btn_back"],
    "buzzer": ["bz"],
    "led_status": ["led", "r1"],
}


def parse_board_pins(text):
    """Extract {signal_name: gpio_number} from the s_pins initializer."""
    pins = {}

    scalar_re = re.compile(r"\.(\w+)\s*=\s*(-?\d+)\s*,")
    for name, value in scalar_re.findall(text):
        if name == "tft_spi_hz":
            continue  # not a GPIO
        v = int(value)
        if v != -1:
            pins[name] = v

    key_match = re.search(r"\.key\s*=\s*\{([^}]+)\}", text)
    if key_match:
        values = [int(v.strip()) for v in key_match.group(1).split(",") if v.strip()]
        for i, v in enumerate(values):
            if v != -1:
                pins[f"key[{i}]"] = v

    return pins


def parse_diagram(text):
    """Return {gpio_number: [connected part id, part type]} for esp:<N> refs."""
    diagram = json.loads(text)
    part_types = {p["id"]: p.get("type", "") for p in diagram.get("parts", [])}

    pin_re = re.compile(r"^esp:(\d+)(?:\.\w+)?$")
    wired = {}
    for conn in diagram.get("connections", []):
        a, b = conn[0], conn[1]
        for this_end, other_end in ((a, b), (b, a)):
            m = pin_re.match(this_end)
            if not m:
                continue
            gpio = int(m.group(1))
            other_id = other_end.split(":", 1)[0]
            wired.setdefault(gpio, []).append((other_id, part_types.get(other_id, "")))

    return wired


def main():
    if not BOARD_C.exists():
        print(f"ERROR: {BOARD_C} not found", file=sys.stderr)
        return 1
    if not DIAGRAM_JSON.exists():
        print(f"ERROR: {DIAGRAM_JSON} not found", file=sys.stderr)
        return 1

    board_pins = parse_board_pins(BOARD_C.read_text())
    if not board_pins:
        print(f"ERROR: parsed zero pin assignments from {BOARD_C} — parser or file is broken",
              file=sys.stderr)
        return 1

    # ESP32-S3 has GPIO0-48 (SOC_GPIO_PIN_COUNT = 49). A value outside that
    # is either a typo in board_devkitc1.c or a parser bug — either way,
    # never pass it through silently.
    out_of_range = {name: gpio for name, gpio in board_pins.items() if not 0 <= gpio <= 48}
    if out_of_range:
        print("ERROR: pin value(s) outside the valid ESP32-S3 GPIO range (0-48):",
              file=sys.stderr)
        for name, gpio in sorted(out_of_range.items()):
            print(f"  {name} = {gpio}", file=sys.stderr)
        return 1

    diagram_pins = parse_diagram(DIAGRAM_JSON.read_text())

    mismatches = []
    not_yet_wired = []

    for signal, gpio in sorted(board_pins.items(), key=lambda kv: kv[1]):
        if gpio not in diagram_pins:
            not_yet_wired.append((signal, gpio))
            continue

        hints = EXPECTED_HINTS.get(signal)
        if hints is None:
            # No Wokwi part convention exists for this signal yet (e.g.
            # sd_cs, i2c_sda/scl) — presence alone is not verifiable.
            continue

        connected = diagram_pins[gpio]
        ok = any(
            any(hint in other_id or hint in other_type for hint in hints)
            for other_id, other_type in connected
        )
        if not ok:
            mismatches.append((signal, gpio, connected, hints))

    board_gpios = set(board_pins.values())
    extra_in_diagram = sorted(g for g in diagram_pins if g not in board_gpios)

    print(f"Parsed {len(board_pins)} pin assignment(s) from {BOARD_C.relative_to(PROJECT_ROOT)}")
    print(f"Parsed {len(diagram_pins)} esp:<N> reference(s) from "
          f"{DIAGRAM_JSON.relative_to(PROJECT_ROOT)}")
    print()

    if not_yet_wired:
        print("Not yet wired in diagram.json (tolerated, not a failure):")
        for signal, gpio in not_yet_wired:
            print(f"  {signal} (GPIO {gpio})")
        print()

    if extra_in_diagram:
        print("WARNING: diagram.json wires GPIO(s) not present in board_devkitc1.c:")
        for gpio in extra_in_diagram:
            parts = ", ".join(pid for pid, _ in diagram_pins[gpio])
            print(f"  GPIO {gpio} -> {parts}")
        print()

    if mismatches:
        print("MISMATCH — board_devkitc1.c and diagram.json disagree:", file=sys.stderr)
        for signal, gpio, connected, hints in mismatches:
            parts = ", ".join(f"{pid} ({ptype})" for pid, ptype in connected)
            print(f"  {signal} = GPIO {gpio} in board_devkitc1.c, expected a part matching "
                  f"{hints}, but diagram.json wires GPIO {gpio} to: {parts}", file=sys.stderr)
        return 1

    print("OK: board_devkitc1.c and diagram.json agree on every wired signal.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
