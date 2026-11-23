# V1 bring-up — Sub-GHz (deferred V0.6 gates)

These are the physical half of ROADMAP V0.6's exit criteria, carried to the V1 prototype
by ADR-0022 and listed as ROADMAP V1 exit 12. **Nothing here has been done.**

V0.6 is complete as *software / RF-protocol verified*. The evidence for that is host
vectors and fault injection, the production service, app and transmit policy run against
a test-only fake CC1101 at the `stark_subghz_transport` seam, the ESP-IDF build, and the
firmware in esp-emu (an empty module slot on the production image; the fake transport on
the test image; docs/VALIDATION.md §3.9). Neither the fake nor the emulator has an
antenna, a crystal, an SPI signal, an RMT peripheral on these pins or a single photon of
RF, so none of the checks below is covered by them. A row is PASS only with the
measurement or observation written in it.

**Scope (SECURITY_SCOPE §2, §3, §4.3).** This is a laboratory procedure on our own
equipment. Every transmitter, receiver and remote used below is ours; every capture is of
our own remote; every transmission goes to our own receiver or into a cable to the
analyser. No third-party remote, gate, car, alarm or code set is captured, decoded
or replayed to run any check, and a check that cannot be done with our own equipment is
not done. Transmit only into a 50 Ω load (a dummy load or an attenuated cable to the
analyser) for checks 20-23 and 25, and with the antenna fitted only for the two checks that name
it (24 and 26), at a time and place where a 10 % duty cycle of 0 dBm on 433 MHz is
permitted (HARDWARE.md §7). The firmware's limits are constants and are not changed to
make a check pass; if a check cannot pass inside them, the check is recorded as FAIL.

## Setup

| Item | Value | Source |
| --- | --- | --- |
| Board | ESP32-S3-DevKitC-1 (the V1 prototype, when it exists) | HARDWARE.md |
| Module | CC1101 breakout, 26 MHz crystal, 433 MHz matching and antenna; 24C02 on `I2C_SDA`/`I2C_SCL` at 0x50 programmed from `test/fake/fake_module_catalog.h` entry 32 (id 0x0A01, caps 0x38) | ADR-0021, ADR-0022 |
| Slot 0 wiring | `MOD_CS` GPIO 40 to `CSn`; `MOD_IRQ0` GPIO 41 to `GDO0` (data in while transmitting); `MOD_IRQ1` GPIO 42 to `GDO2` (data out); SCLK/MOSI/MISO on the shared SPI2 pins; 3V3 and GND; `MOD_EN` not connected (tied high) | HARDWARE.md §6 |
| SPI clock | 4 MHz (`STARK_SUBGHZ_SPI_HZ`; the chip allows 6.5 MHz for bursts) | `components/stark_subghz/Kconfig.stark` |
| Firmware | production image, the main SHA under test: `________` | — |
| Own transmitter | a 433.92 MHz OOK remote or sensor we own, datasheet or own capture of its timing written here: `________` | — |
| Own receiver | the matching receiver (or a second CC1101 module in receive) that reacts to that transmitter: `________` | — |
| Instruments | spectrum analyser (or SDR with a calibrated attenuator) covering 433 MHz; 20-30 dB attenuator and 50 Ω load; logic analyser with at least 4 channels at 10 MS/s; oscilloscope; multimeter or current probe; our own 433 MHz antenna | — |

## What the software cannot show

The fake and the emulator cannot tell us any of these, and each has a check below.

* The **CC1101's own behaviour**: the real `CHIP_RDYn` wait after chip select (the firmware
  does not wait for the edge yet; it relies on the chip being ready after reset), reset
  timing, and that the analogue register values (`FSCAL`, `TEST`, `AGCCTRL`, the 203 kHz
  channel filter: the datasheet's recommended 433 MHz settings) are good on this board.
* The production **RMT receive path** on GDO2: 1 µs resolution, 64-symbol frames, a 20 ms idle
  threshold ending a burst, ping-pong re-arm. The hardware does not report the low time that
  ends a frame; `stark_subghz_transport_rx_take()` appends a 20 ms low when a frame ends high.
  That 20 ms is a guess about what a real remote's repeat gap looks like until check 14.
* The production RMT **transmit path** on GDO0: a channel created for one transmission, a
  streaming encoder, durations over 15 bits split, the line ending low, the channel deleted
  after every outcome. In asynchronous OOK a **high on GDO0 is carrier**: the data line
  must end low, or the carrier stays up after the symbols are over (check 20).
* **Power, on-air time, spurious emissions and antenna matching.** `PATABLE` reads back as
  `{0x00, 0x60, 0, ...}`; whether that is about 0 dBm on this board is a measurement.
* The **shared SPI bus** with the panel and the card, and the module hot-plugged.

## Checks

Checks 1-9 need no antenna and no transmission. Do them first, in order: a failing one stops
the rest.

| # | Check | How | Pass criterion | Result | Evidence |
| --- | --- | --- | --- | --- | --- |
| 1 | Wiring continuity | Multimeter, board and module unpowered | `MOD_CS`, `MOD_IRQ0`, `MOD_IRQ1`, SCLK, MOSI, MISO, 3V3, GND reach `CSn`, `GDO0`, `GDO2`, `SCLK`, `SI`, `SO`, `VDD`, `GND` and nothing else | | |
| 2 | Module descriptor | Boot with the module; Modules app, slot 0 | `module: slot 0 CC1101 433 MHz id=0x0a01 rev=1 caps=0x00000038`; `module: slot 0 claimed by subghz` | | |
| 3 | Chip identification | Boot log | `subghz: cc1101 version 0x14` (or `0x04`), then a `module: caps` line with bit 5 (0x20) set; the Sub-GHz row is enabled | | |
| 4 | SPI signal at 4 MHz | Logic analyser on `CSn`, SCLK, MOSI, MISO during a meter session | Mode 0, 4 MHz, `CSn` low only during a transfer, status byte plausible; record the rise time and any ringing | | |
| 5 | `CHIP_RDYn` and reset timing | Logic analyser on `CSn` and MISO at the identification, and at a re-identification after a lost module | After `CSn` falls MISO goes low within the datasheet's crystal start-up; record the time. The firmware starts talking before it: record whether the reset still completes (`subghz: module not usable: timeout` must not appear). If it can fail, a MISO wait is a new task, not a patch here | | |
| 6 | Wrong or absent chip | A module with no chip fitted; a different SPI part on the slot | `subghz: not a cc1101 (...)`, capability not set, row dimmed, no panic; retried at 2, 4, 8 s and then every 8 s, logged once | | |
| 7 | Hot-plug | Unplug and re-plug the module with the app open | `subghz: module lost` within 3 s, the app returns to its home list, no reset; back: `subghz: cc1101 version 0x14` and the row works again. No brown-out, no display glitch | | |
| 8 | Shared SPI with the panel and the card | Meter running while the panel redraws and a 100 kB card write test runs; logic analyser on all chip selects | No display corruption, the card test byte-identical, `CSn` of the module high during panel and card traffic, MISO released when deselected. Record the worst band-flush stall (ROADMAP V0.5 exit 3, V1 exit 11) | | |
| 9 | Supply and idle current | Multimeter on module 3V3 | Record RX, IDLE, and TX at 0x60; within the 300 mA per-module budget | | |
| 10 | RSSI meter: noise floor | Meter at 433.920 MHz, antenna fitted, nothing transmitting | A steady level near the band's noise floor (record it) that moves by less than 3 dB without an LO nearby | | |
| 11 | RSSI against a known level | Signal generator or the attenuated analyser tracking output into the antenna port at -90, -80 ... -40 dBm | Record the meter against the known level; the offset assumed in firmware (74 dB) is within ±6 dB, or the difference is written here as a V1 correction | | |
| 12 | RSSI shows our own transmitter and tracks distance (ROADMAP V0.6 exit 1) | Our own remote held down at 0.5, 1, 2, 4 and 8 m | The meter at its frequency rises above the floor while the key is down; the level falls as the distance grows, monotonically within ±3 dB of noise. Record every distance | | |
| 13 | Band scan | Band scan with our own remote held down | `subghz: peak <f> MHz <dBm> dBm` within one channel (25 kHz) of the transmitter's real frequency (read on the analyser); the sweep takes ≤ 500 ms, record it; the radio is idle afterwards (`subghz: idle`) | | |
| 14 | Raw capture reproduces documented timing (ROADMAP V0.6 exit 2) | Capture our own remote 20 times; Save; open on a PC | Timing histogram bins within ±1 symbol tick and ±10 % of the remote's datasheet (or of a logic-analyser capture of its encoder output); the burst gaps and repeat count recorded; the 20 ms frame-end low does not merge repeats or split a burst wrongly. Record any capture where it did | | |
| 15 | 2-FSK capture | Capture a 2-FSK source of our own, if we have one | The capture file parses on a PC; if none exists the row is N/A and says so | | |
| 16 | Decode our own remote | Decode on the captured file | The analysis view's kind, bit count, repeats and short/long (or T) µs match what the remote's datasheet or our logic-analyser capture says; the hex equals the code we know it sends | | |
| 17 | Capture limits | A signal longer than 2 s; a continuous carrier; noise only | The capture ends at 2 s or 2048 symbols, never hangs, never overflows; `subghz: capture ... sym` is logged; the radio returns to idle | | |
| 18 | Receive never transmits | Analyser on GDO0 and on the RF port through a whole meter, scan, capture and decode session | GDO0 stays high impedance or low and no carrier appears at 433 MHz; `PATABLE` unchanged | | |
| 19 | Transmit refusals on silicon | Replay with the boot lock running (first 18 s), straight after a send, from a 2-FSK file | `subghz: tx refused: offtime` / `file`; nothing on GDO0 or the RF port | | |
| 20 | GDO0 and the RMT transmit path | Logic analyser on GPIO 41, RF port into the analyser through 30 dB and a dummy load; confirm one replay of our own capture | One burst whose high/low times equal the file's symbols to within 2 µs (record the maximum difference); GDO0 ends low and the carrier is gone within one symbol of the last one; no pulse before or after the transmission | | |
| 21 | Power at `PATABLE` 0x60 | Analyser, peak hold, 100 kHz RBW, attenuation corrected, dummy load | Conducted power recorded in dBm; pass only if it is ≤ +3 dBm. Record the value read back from `PATABLE` too. If the module reads higher, the cap is FAIL, not a reason to raise the limit: lower it in a new task | | |
| 22 | On-air time and duty | Analyser zero-span or a power detector on a scope; one replay; then as many as the policy allows in an hour | On-air time equals the file's duration (±2 %, plus the ramp); the gap before the next permitted send is ≥ 9× the last; the accepted total over any rolling hour is ≤ 360 s. Record the log lines `subghz: tx ...` next to the analyser timestamps | | |
| 23 | Frequency, occupied bandwidth and spurious | Analyser, span 10 MHz and 1 GHz | Carrier within ±50 kHz of the file's `freq`; no emission outside 433.05-434.79 MHz above the SRD limit; record the second and third harmonics | | |
| 24 | Replay triggers our own receiver (ROADMAP V0.6 exit 3) | Own transmitter captured, Save, Replay with the antenna fitted at 1 m from our own receiver | The receiver reacts exactly as it did to the original remote; record the number of replays tried and the number that worked. Nothing else in range is triggered | | |
| 25 | Boot lock and power cycle | Power-cycle the device between replays | The first replay is refused as `offtime` for 18 s after every boot, and the accepted sends stay within 10 % | | |
| 26 | Antenna and matching | Antenna on and off, matching network values recorded | Return loss or RSSI change recorded; the board in its final enclosure repeated once | | |
| 27 | Frame rate and latency while receiving | Meter and capture running with the panel at its normal use | Frame rate and key-to-pixel figures within the V1 targets in docs/measurements.md; no `diag:` event drops | | |
| 28 | Stack and heap on silicon | A 10-minute session using all views, then the diagnostics app | Task stack of `stark_subghz` well under its 4 096 B (the V0.6 emulator peak is recorded in docs/measurements.md); the heap returns to its boot value after the app stops | | |

## Pass rules

* A transmit check (20-26) is attempted only after checks 1-19 have passed and the analyser
  is connected.
* Any emission outside 433.05-434.79 MHz, any carrier with no `subghz: tx` log line, a
  `PATABLE` byte other than 0x00 or 0x60, or more than 360 s of accepted air time in an hour
  stops the bring-up and is a defect, not a result to record and move past.
* Results also go into `docs/measurements.md` -> "Hardware (gated, V1 prototype)", Sub-GHz
  rows, and every FAIL is a task in TASKS.md before V1 closes.
