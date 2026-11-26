# V1 bring-up — functional re-verification (exit 1)

ROADMAP V1 exit 1: "every V0–V0.7 functional exit criterion re-verified on hardware where
applicable". Written by STARK-0806. **Nothing here has been done.**

Every V0–V0.7 functional criterion was met in software: host tests, the ESP-IDF build and
the production firmware in esp-emu. This procedure repeats those flows on silicon. It
does this in two ways:

* **Replays.** The production-image emulator scenarios replayed by hand on the breadboard.
  `scripts/check_hw_log.py` (STARK-0805) prints the key sequence as operator steps. It then
  checks the console capture with the emulator's own rules: line order, forbidden output,
  heap gates and `drops=0`.
* **Traceability (§3).** A table maps each criterion to the replay, the check in another
  procedure, or the reason it has no hardware half.

A row is PASS only with its evidence written in it: the capture file's name and the
script's `PASS` line.

**Scope (SECURITY_SCOPE §1).** Our own board and parts. 3.3 V logic only
([v1-parts.md](v1-parts.md)).

## 1. Setup — bench E

The scenarios state what the firmware prints **in the emulator's world**, so they are
replayed on a breadboard built to match it. This is "bench E": the [v1-core.md](v1-core.md)
breadboard, after that procedure's power, boot and display checks have passed, with:

| Item | Value | Why |
| --- | --- | --- |
| microSD | **No card** in the breakout | esp-emu models no card: `storage: no card` (ADR-0018) |
| PN532 | Not fitted | esp-emu has no PN532: `nfc: no module` |
| Sub-GHz module | Not fitted | No radio model: `subghz: no module` |
| IR front-end | Nothing on GPIO 47 / 48 (no LED driver, no receiver) | The IR self-test must time out as in `v03-ir-noloop` |
| Slot 0 ID EEPROM (0x50) | A 24C02 holding the `zeroes` image: `scripts/gen_module_vectors.py --emit DIR zeroes` | esp-emu answers at 0x50 and reads zeros: `module: slot 0 generic (bad magic)`, the slot's three lab lines withdrawn (`lab: pins 38 39`) |
| Slot 1 (0x51) and the rest of I²C | Nothing | esp-emu NACKs every other address: the I²C scan finds `0x50` only |
| Lab pins 38 / 39 | Nothing connected | `v07-gpio-prod` reads an open input with a pull-up as high; `v07-measure-prod` sees no edges |
| Board revision | v1.0 / v1.1: `________` (v1.0 has its RGB LED on GPIO 48, `IR_RX`) | Record it; a spurious IR frame on v1.0 is a v1-ir check 2 finding, not a fail here |
| Firmware | Production image, the main SHA under test: `________`; breadboard settings as v1-core.md's setup: `________` | — |
| Console | USB, 115200 Bd. Capture with `idf.py -p PORT monitor \| tee LOG`, started before the reset that begins each scenario | `check_hw_log.py` ignores the monitor's CR and colours |

Scenarios that expect the default settings begin with an erase. `--steps` says so: run
`idf.py -p PORT erase-flash`, then flash.

Running one scenario:

```bash
python3 scripts/check_hw_log.py --steps test/emu/v0-boot-and-menu.toml
```

Follow the steps, then check the capture:

```bash
python3 scripts/check_hw_log.py test/emu/v0-boot-and-menu.toml LOG
```

The steps keep the scenario's order, but the timing is human. A step that does not do what
the scenario says is a failure to record. A mistyped sequence is a reason to rerun, not to
pass: start again from the reset and keep both captures.

## 2. Replays

Exactly the scenarios `check_hw_log.py --list` reports as replayable. The others need the
storage test image, the RMT loopback, the UART1 peer, pokes or the NVS erase over the
control channel. Their physical halves are the checks named in §3.

| # | Scenario | Criteria (§3) | Result | Evidence (capture, `PASS` line) |
| --- | --- | --- | --- | --- |
| R1 | `test/emu/v0-boot.toml` | V0 4, 6 | | |
| R2 | `test/emu/v0-boot-and-menu.toml` | V0 2 | | |
| R3 | `test/emu/v0-input-matrix.toml` | V0 2 | | |
| R4 | `test/emu/v01-navigation.toml` | V0.1 2 | | |
| R5 | `test/emu/v01-dialog.toml` | V0.1 2 | | |
| R6 | `test/emu/v01-app-lifecycle.toml` | V0 7 | | |
| R7 | `test/emu/v01-diag.toml` | V0.1 4 | | |
| R8 | `test/emu/v01-displaytest.toml` | V0 5 | | |
| R9 | `test/emu/v01-hello.toml` | V0.1 1 | | |
| R10 | `test/emu/soak/v01-soak.toml` (about 65 s; 97 presses) | V0 6, 7; V0.1 3 | | |
| R11 | `test/emu/v02-settings-persist.toml` (erase first; two resets) | V0.2 1 | | |
| R12 | `test/emu/v02-storage-boot.toml` | V0.2 4 | | |
| R13 | `test/emu/v02-no-card.toml` | V0.2 4 | | |
| R14 | `test/emu/v03-ir-rx.toml` | V0.3 2 | | |
| R15 | `test/emu/v03-ir-noloop.toml` | V0.3 2 | | |
| R16 | `test/emu/v04-nfc-boot.toml` | V0.4 3 | | |
| R17 | `test/emu/v04-nfc-absent.toml` | V0.4 3 | | |
| R18 | `test/emu/v05-empty-bus.toml` | V0.5 2 | | |
| R19 | `test/emu/v06-subghz-absent.toml` | V0.6 4 | | |
| R20 | `test/emu/v07-lab-boot.toml` | V0.7 3 | | |
| R21 | `test/emu/v07-gpio-prod.toml` | V0.7 3 | | |
| R22 | `test/emu/v07-i2c-prod.toml` | V0.7 1, 3 | | |
| R23 | `test/emu/v07-spi-prod.toml` | V0.7 3 | | |
| R24 | `test/emu/v07-measure-prod.toml` | V0.7 4 | | |
| R25 | `test/emu/v07-selftest-prod.toml` | V0.7 5 | | |

Two checks the scenarios cannot make, because the emulator has no panel to look at:

| # | Check | How | Pass criterion | Result | Evidence |
| --- | --- | --- | --- | --- | --- |
| R26 | Partial update equals a full redraw (V0 5) | Root menu: move the selection with DOWN (partial updates), photograph the screen; open and leave About (a full refresh, `display: full refresh`), photograph again at the same selection | The two photographs show the same picture; no stale pixels, seams or offsets | | |
| R27 | Diagnostics is live (V0.1 4) | Open Diagnostics for 10 s, watching it and the console | Heap and FPS on screen change once a second and match the `diagnostics: heap=… fps=…` lines | | |

## 3. Traceability — every V0–V0.7 functional criterion

"Where applicable": a criterion about the source tree, the build or the host suite has no
hardware half and stays met by CI. Memory is deterministic and stays a simulation gate
(ROADMAP V0 exit 6). On silicon it is only recorded (R1, R10, v1-core check 34).

| Milestone exit | Criterion (short) | On hardware |
| --- | --- | --- |
| V0 1 | Clean build, warnings as errors | Not applicable: CI `firmware` job; the image flashed is that artifact |
| V0 2 | Boot → navigate → launch → BACK | R2, R3 |
| V0 3 | Host tests and coverage | Not applicable: CI `host` jobs |
| V0 4 | `boot: ui_ready` logged in order | R1; the figure itself is v1-core check 4 |
| V0 5 | Rendering correct: no seams, partial = full | v1-core check 9 (seams), R26, R8 |
| V0 6 | ≥ 200 kB heap at the root; launch/exit within 1 kB | R1 (`heap_min`), R10 (`heap_delta_max` 1 kB across every app twice); the simulation gate stands |
| V0 7 | Zero drops, zero render errors | Every replay (`drops=0`, `ui: render failed` forbidden); R6, R10; v1-core checks 19, 33 |
| V0 8 | No simulator `#ifdef` | Not applicable: source tree (`scripts/check.sh`) |
| V0.1 1 | A hello app in one directory | Not applicable as written (source); R9 shows it runs on silicon |
| V0.1 2 | Modal confirm dialog | R5; long-BACK through a dialog R4 |
| V0.1 3 | Drops zero over 60 s | R10; v1-core check 19 |
| V0.1 4 | Diagnostics shows live heap and FPS | R7, R27 |
| V0.2 1 | Settings survive reboot | R11 (reset); v1-core check 23 (power cycle) |
| V0.2 2 | 100 kB byte-identical while rendering | v1-core checks 25, 26, 30 |
| V0.2 3 | Card pulled mid-write degrades gracefully | v1-core checks 27, 28 |
| V0.2 4 | Mount failure leaves the device usable | R12, R13 (no card); v1-core check 29 (non-FAT, faulty) |
| V0.3 1 | ≥ 20 vectors per protocol | v1-ir check 9 |
| V0.3 2 | Capture, save, replay, own device responds | v1-ir check 10; R14, R15 (receive off at boot; no echo, no panic) |
| V0.3 3 | Raw capture/replay of an unsupported protocol | v1-ir check 11 |
| V0.4 1 | ≥ 20/20 UID and technology reads | v1-nfc checks 7, 8 |
| V0.4 2 | NDEF URI written and read back | v1-nfc checks 9, 10, 12 |
| V0.4 3 | Module absence detected, app disabled | R16, R17; v1-nfc checks 3, 4 |
| V0.5 1 | Plugging a module changes the app list | v1-modules check 21 |
| V0.5 2 | Unknown or absent EEPROM → generic entry, no boot failure | R18; v1-modules checks 3, 4 |
| V0.5 3 | Two modules share SPI without corrupting the display | v1-modules check 12 (V1 exit 11) |
| V0.6 1 | RSSI shows our transmitter, tracks distance | v1-subghz check 12 |
| V0.6 2 | Raw capture reproduces documented timing | v1-subghz check 14 |
| V0.6 3 | Replay triggers our own receiver | v1-subghz check 24 |
| V0.6 4 | TX needs confirmation, respects the duty cap | v1-subghz checks 19, 21, 22; R19 (no radio: nothing claimed, nothing sent) |
| V0.7 1 | I²C scanner finds devices at expected addresses | R22 (`0x50` only on bench E); v1-labtools checks 20, 21 |
| V0.7 2 | UART bridge round-trips at 115200 | v1-labtools checks 28, 29 (the `v07-uart-prod` peer is emulator-only) |
| V0.7 3 | Reserved pins cannot be configured | R20–R23; v1-labtools checks 1, 2, 4, 6 |
| V0.7 4 | Frequency and duty measurement | R24 (no edges on an open pin); v1-labtools checks 15–17 |
| V0.7 5 | Self-test and export | R25; v1-labtools checks 33–35 |

## After the procedure

A FAIL becomes a task in TASKS.md. A scenario is not edited to make a hardware row pass:
if silicon legitimately prints something else (a real chip's revision, a timing the
emulator does not have), the difference is written into the row, and a change to the
scenario's expectation, if any, is its own task. Once R1–R27 are PASS and every check
§3 names is PASS in its own procedure, ROADMAP V1 exit 1 can be marked against this
document.
