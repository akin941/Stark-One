# V1 bring-up — infrared (deferred V0.3 gates)

The physical half of ROADMAP V0.3's exit criteria, carried to the V1 prototype by
ADR-0019 and listed as ROADMAP V1 exit 9. **Nothing here has been done.** V0.3 is
complete as *IR software / protocol verified*: host vectors and fault injection, the
ESP-IDF build, and the production firmware in esp-emu with its RMT TX looped into RX
(docs/VALIDATION.md §3.6). That loopback carries the pre-carrier baseband only — no
carrier, no LED, no light, no receiver — so none of the checks below is covered by it.
A row is PASS only with the measurement or observation written in it.

**Scope (SECURITY_SCOPE §2, §4).** Every remote and every target device used below is
our own. Recorded vectors come from our own remotes only (§4.7).

## Setup

| Item | Value | Source |
| --- | --- | --- |
| Board | ESP32-S3-DevKitC-1 (record the revision: v1.0 has its RGB LED on GPIO 48) | HARDWARE.md §2.2 |
| `IR_TX` | GPIO 47 → NPN driver → IR LED | HARDWARE.md, `stark_board` |
| `IR_RX` | GPIO 48 ← TSOP38238-class 38 kHz demodulator (active-low output) | HARDWARE.md, `stark_board` |
| Firmware | production image, the main SHA under test: `________` | — |
| RX settings | RMT 1 MHz, glitch filter 1 250 ns, idle 12 ms | `stark_ir.c` (STARK-0303) |
| TX settings | carrier 36 kHz (RC5/RC6), 38 kHz (NEC/NECX), 40 kHz (SIRC); duty 33 %; ≥ 1 s between transmissions | `ir_tx_guard.h` (STARK-0304) |
| Instruments | logic analyser (≥ 1 MS/s), multimeter or current probe, phone camera (IR visible) | — |

Note: a 38 kHz receiver also demodulates 36 and 40 kHz carriers, with less range; record
what is observed, do not tune the firmware to one remote.

## Checklist

| # | Check | How | Expected | Result | Date / who |
| --- | --- | --- | --- | --- | --- |
| 1 | GPIO 47/48 idle levels and signal integrity | Logic analyser on both pins, IR app closed, then open | TX low (LED off) when idle; RX high when no light; clean edges, no ringing that crosses the threshold | | |
| 2 | DevKitC-1 v1.0 RGB LED on GPIO 48 | Observe while receiving | Flicker at most (cosmetic, HARDWARE.md §2.2); RX decoding unaffected | | |
| 3 | IR LED current through the NPN driver | Current at the LED during a long mark (App Test IR self-test) | Within the design value of the driver resistor (record design vs measured) | | |
| 4 | Carrier frequency per protocol | Logic analyser on the LED side, App Test IR self-test | RC5/RC6 36 kHz, NEC/NECX 38 kHz, SIRC 40 kHz, each ± 1 % | | |
| 5 | Carrier duty | Same capture | 33 % ± 3 % | | |
| 6 | Frame timing on silicon | Same capture, envelope vs `docs/ir-format.md` | Each mark/space within the decoder tolerance (± max(25 %, 60 µs)); record the worst deviation | | |
| 7 | Transmit rules on hardware | Two sends < 1 s apart; a send without confirming | Second send refused ("Wait a moment"); nothing leaves the LED before the dialog's OK; every send logged (`ir: tx …`) | | |
| 8 | Receiver demodulation | Own remote at ~1 m, IR app open; logic analyser on GPIO 48 | Active-low envelope of the remote's frame; `ir: rx <proto> …` with the remote's address/command | | |
| 9 | ≥ 20 recorded vectors per protocol | Own remotes, per protocol supported (NEC, NECX, RC5, RC6-0, SIRC-12/15/20); save each, export from the card | ≥ 20 per protocol decoded to the remote's values; files kept as recorded vectors (ADR-0019 §3) | | |
| 10 | Capture → save → replay → own device responds | IR app: capture own remote → Save → Browse → send | The own device performs the function; the replay is received back (`ir: rx` same fields) | | |
| 11 | Raw replay of an unsupported protocol | An own remote no decoder accepts | Captured as `RAW n=…`, saved, replayed; the own device responds | | |
| 12 | Optical self-test | App Test → IR self-test with the LED aimed at the board's own receiver | 7/7 decoded back — the first optical end-to-end evidence (the emulator 7/7 is baseband only) | | |
| 13 | Range | Own remote and own LED at 1 m, 3 m, 5 m, on axis | Record the furthest reliable decode both ways | | |
| 14 | Angle | At 1 m: 0°, 30°, 60° off axis | Record | | |
| 15 | Ambient light and noise | Daylight near a window, LED lighting, a CFL; idle captures over 60 s | Record spurious `ir: rx` (expected: none decoded; RAW noise counted) | | |
| 16 | Own echo while transmitting | IR app open while sending | Record whether the board receives its own send (the loopback emulator always does; hardware depends on geometry) | | |
| 17 | Heap and stability with the IR front-end fitted | `diag:` lines over the V0.3 soak's key path on hardware | Heap within 1 kB first vs last; drops 0 | | |

Results also go into `docs/measurements.md` → "Hardware (gated, V1 prototype)", IR rows.
