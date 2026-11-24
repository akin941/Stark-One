# V1 bring-up — lab tools (deferred V0.7 gates)

These are the physical half of ROADMAP V0.7's exit criteria, carried to the V1 prototype by
ADR-0023 and listed as ROADMAP V1 exit 13. **Nothing here has been done.**

V0.7 is complete as *software / lab-protocol verified*. The evidence for that is host
vectors and fault injection against independent Python references, the production services
and apps run over a test-only pad, bus, SPI and UART model at the `stark_lab_transport`
seam, the ESP-IDF build, and the firmware in esp-emu (the production image on its one I²C
device and its UART1 bridge; the test image with the fake lab transport; docs/VALIDATION.md
§3.10 and `docs/measurements.md` "V0.7 exit criteria"). Neither the model nor the emulator
has a pad that moves, a wire that carries a signal, a drive strength, a clock edge, a
pull-up, a connector or a real peripheral that answers, so none of the checks below is
covered by them. A row is PASS only with the measurement or observation written in it.

**Scope (SECURITY_SCOPE §1, §2, §4.6).** This is a laboratory procedure on our own
equipment. Every pin, bus, module and instrument used below is ours; nothing is connected
to hardware we do not own, and no tool is pointed at a device it does not own. The firmware's
limits (the offered pins, weakest drive strength, the arm and confirmation, the I²C write
refusals, the SPI eligibility rule, the measurement range, the UART formats) are constants
and are not changed to make a check pass; if a check cannot pass inside them, the check is
recorded as FAIL and becomes a task. Logic levels are **3.3 V only**: nothing in this
procedure applies more than 3.3 V (3.6 V absolute ceiling for any source) to a lab pin, the
I²C bus or the SPI bus, ever, and no check needs it.

## Setup

| Item | Value | Source |
| --- | --- | --- |
| Board | ESP32-S3-DevKitC-1 (the V1 prototype, when it exists). Board revision (v1.0 or v1.1) written here: `________` | HARDWARE.md |
| Lab pins | `LAB_UART_TX` GPIO 38 and `LAB_UART_RX` GPIO 39, each through a 330 Ω series resistor on the breadboard; slot 0 `MOD_CS` 40, `MOD_IRQ0` 41, `MOD_IRQ1` 42 while the slot is empty | HARDWARE.md §2.4, §6 |
| Shared buses | I²C `I2C_SDA` 1 / `I2C_SCL` 2 (pull-up values written here: `________`); SPI2 on `SPI_SCLK`, `SPI_MOSI`, `SPI_MISO` with the panel's and the card's chip selects | HARDWARE.md §2.2 |
| Firmware | production image, the main SHA under test: `________` | — |
| Signal generator | a function or pulse generator with a 3.3 V logic output (0 V low, 3.3 V high, **not** its default 5 V or 10 V; set and verified with a scope before the first connection), 1 Hz to 100 kHz, adjustable duty, a frequency reference better than 0.01 %; model and calibration date: `________` | — |
| Logic analyser and scope | at least 8 channels at 100 MS/s (the 40 kHz PWM and the 1 µs UART bit at 921 600 Bd need it); an oscilloscope of at least 100 MHz with 10x probes; a multimeter and a milliammeter | — |
| Loads | 330 Ω, 1 kΩ, 10 kΩ and 100 kΩ resistors (1 %), a 1 nF capacitor, a tactile switch (for bounce) | — |
| UART adapter | a USB-serial adapter that is **3.3 V TTL** (jumper or datasheet checked, level measured on its TX before connecting); a terminal on the PC that shows hex | — |
| I²C parts | at least three 3.3 V breakouts of ours at known addresses (one with a WHO_AM_I register, one with a harmless writable register), written here with their addresses: `________` | — |
| SPI parts | a SPI NOR flash breakout (a W25Q-type with a 0x9F JEDEC ID) wired on a SPI breakout module (catalogue entry 34) on the slot's `MOD_CS`; and the CC1101 module of V0.6 (catalogue entry 32) for the refusal | docs/module-format.md |
| Card | a FAT32 microSD card of ours, not holding anything else | — |
| Modules | a programmed 24C02 module image for each entry used, written off the device (the firmware cannot write one) | docs/module-format.md |

## What the software cannot show

The model and the emulator cannot tell us any of these, and each has a check below.

* **That the offered pins are the only reachable ones.** The host sweeps every number from
  -1 to 64 against every call; whether the pin map in `stark_board` matches the board's real
  wiring (so that "reserved" really is display, card, buttons, buzzer, LED, I²C, IR, USB,
  console, strapping and flash/PSRAM) is a fact about the board.
* **Electrical behaviour of a pad.** Drive strength (`GPIO_DRIVE_CAP_0`), the output levels,
  the weak internal pulls, hi-Z with no pull after every exit, the read-back that detects a
  contention, and the idle-level short that the read-back cannot see (a pulse into a short to
  its own idle level is not detected; ADR-0023 0702 addendum 6).
* **The waveforms.** LEDC PWM on a pad (esp-emu routes none to a pad, docs/VALIDATION.md §2),
  pulse widths, and the real edge interrupt: latency, the asymmetry between the two edges
  that shows up as a duty error, and whether a flash operation during a window disturbs a
  stamp (the handler is in IRAM for that reason, ADR-0023 0703 addendum 1).
* **The measurement accuracy against a real signal.** The host proves the arithmetic against an
  independent reference on synthetic, jittered edge sets (±0.5 % to 10 kHz, ±2 % to 50 kHz;
  ±2 and ±5 points of duty); a real source is a V1 measurement.
* **The PWM-into-RX loop of the plan.** ADR-0023 §4 said the V1 check wires `LAB_UART_TX`
  PWM into `LAB_UART_RX`. That needs two live pins at once and the one-pin GPIO app cannot hold
  them (BACK from a pin releases its PWM; ADR-0023 0703 addendum 10). It is restated below as
  independent references on each side (checks 12, 15, 16 and 17): the device's own PWM on a
  logic analyser, and the device's measurement of a signal generator. The wire is not used.
* **I²C and SPI against real parts**: the addresses that answer, pull-ups and waveforms at 100
  and 400 kHz, a device pulled mid-sweep and the bus afterwards, a real JEDEC ID, and the panel
  and the card staying intact while the probe uses the shared SPI bus.
* **The UART**: real baud rates (the S3 model has no baud timing), levels, an adapter on the
  other end, the highest baud the edge capture can follow (a bit at 921 600 Bd is 1.09 µs),
  a glitch on a wired line when the driver is installed again, and a long log to a card.
* **The self-test on the breadboard**: the flash and RAM checks on silicon, and each attested
  check (display, buttons, buzzer, status LED) really observed by a person.

## The lab header (a V1 hardware requirement)

The board spends all 25 usable GPIOs, so a lab tool offers at most five pins (two with a
module fitted in slot 0), and the GPIO app holds one at a time. ROADMAP's V1 hardware list
carries a **lab header** of spare, series-protected, level-limited pins as a requirement of
the V1 PCB. V0.7 did not invent it and this procedure does not need it: every check below runs
on the breadboard pins with a generator, an analyser and our own parts. When the header exists,
re-run checks 1-3, 5-9, 12 and 15-19 on it, and record here which pins it offers and what limits
protect each.

## Checks

Checks 1-11 need nothing connected to a lab pin except an instrument. Do them first, in
order: a failing one stops the rest.

| # | Check | How | Pass criterion | Result | Evidence |
| --- | --- | --- | --- | --- | --- |
| 1 | The offered pins, slot 0 empty and fitted | Boot with slot 0 empty (nothing on the I²C bus answering 0x50), open GPIO; then fit the catalogue-34 breakout and open it again | Empty: the home list is exactly pins 38, 39, 40, 41, 42 (`lab: pins 38 39 40 41 42`, `gpioapp: home 38 39 40 41 42`). Fitted: exactly 38 and 39. No other number is offered, in any mode, not even to read | | |
| 2 | Reserved pins cannot be reached | Scope or logic analyser on every other GPIO that reaches a header or a part (display `TFT_*`, `SD_CS`, backlight, buzzer, status LED, the six buttons, `IR_TX`, `IR_RX`, `I2C_SDA`, `I2C_SCL`, USB 19 and 20, console 43 and 44, strapping 0, 3, 45, 46); run a whole session of GPIO (every mode on every offered pin), I²C, SPI, UART and Self-Test | No reserved pad changes level or direction because of a lab tool, beyond what its own owner does (the panel redrawing, the log being written); the GPIO app never lists one. Record the pads watched | | |
| 3 | Nothing drives a lab pin at boot or after a reset | Hold 38 and 39 (and 40, 41, 42 with an empty slot) at a known level through 100 kΩ from the generator's output set to 0 V, then 3.3 V; power-cycle and reset the board | The pad stays at the externally imposed level (within 3 % of the divider) from the first instant of reset through the boot, until an app is opened and a pin is armed; no glitch above 100 mV on a scope | | |
| 4 | Drive strength and output levels (weakest drive) | GPIO app, pin 38 as an output, armed through the confirmation; measure the pad into no load, into 10 kΩ, 1 kΩ and 330 Ω to ground (high) and to 3.3 V (low); the current with the milliammeter in series with the 330 Ω | Levels and currents recorded and within the ESP32-S3 datasheet's DC characteristics for the lowest drive setting (value and datasheet page in the evidence column); the current into 330 Ω never exceeds the datasheet's I_OH for that setting. The firmware never needs a pad to source more than the weakest drive allows | | |
| 5 | Pulls and the pad probe | Pad 39 as an input: pull none, up, down against a multimeter at 10 MΩ; then the confirmation's "reads: floating / high / low" with the pad open, held high through 1 kΩ and held low through 1 kΩ | The pull resistance (voltage across a 100 kΩ to the opposite rail) is recorded and inside the datasheet's pull range; the probe says `floating`, `high` and `low` for the three cases and the pad is released afterwards (check 6) | | |
| 6 | Hi-Z and no pull after every exit | Pad driven high, then each of: BACK from the pin, leaving the app, a contention, a hot-plug withdrawal (check 9), a transport fault (a pad configuration failure is hard to cause on silicon, so this exit is host only and is noted here as N/A); then a 100 kΩ divider from the generator into the pad | After each exit the pad is entirely set by the external divider (within 3 %): no drive, no pull; `lab: released pin <n>` or `released all` is logged each time | | |
| 7 | Contention is detected and bounded | Pad 38 driven high into 330 Ω to ground, and driven low into 330 Ω to 3.3 V (never a direct short); then a pulse whose idle level is shorted through 330 Ω | `lab: contention pin 38` and the alert on the first drive; the pad is hi-Z within 2 ms (scope) and the arm is gone (the next drive asks again); the supply current of the board changes by less than the 330 Ω allows. For the idle-level short the read-back cannot see: the pulse completes and the pad is restored, and the result is recorded as the documented limit, not a pass of detection | | |
| 8 | Arm and confirmation on the pad | Output, PWM and pulse on pin 38 with a scope on it: BACK on the confirmation, then confirm | Nothing is driven (pad stays at its external level or hi-Z) until the second OK; after it, exactly the requested drive; leaving the app and re-entering asks again | | |
| 9 | Hot-plug withdrawal of a driven slot pin | Empty slot 0, GPIO app, pin 40 an output driven low with the scope on it, then plug the catalogue-34 breakout into slot 0 | Within the module table's detection time the pad goes hi-Z, `lab: pin 40 withdrawn (slot 0)` (also 41 and 42) and the alert appear, the home list drops the three slot pins; unplugging offers them again. Record the time from insertion to hi-Z | | |
| 10 | 3.3 V only and series protection | Before anything is connected: measure the generator's high level, the adapter's TX idle and high levels, and any breakout's output with a multimeter and the scope; inspect the 330 Ω series resistors on 38 and 39 | Every source is at most 3.3 V + 5 % and never above 3.6 V; the series resistors are present on both lab UART lines. Record each level | | |
| 11 | DevKitC GPIO 38 RGB LED caveat | On a rev v1.1 board, GPIO app, pin 38 an output high behind the confirmation; on a rev v1.0 board the LED is GPIO 48 and not a lab pin | v1.1: the on-board LED lights and the confirmation text says own hardware only; the pad level and drive of check 4 are measured with the LED loading it and the difference is written here; v1.0: no LED reacts. Record the revision | | |
| 12 | PWM frequency and duty on a logic analyser | GPIO app, pin 38 PWM at 100 Hz, 1 kHz, 10 kHz and 40 kHz, each at duty 1, 25, 50, 75 and 99 %, then 100 %; analyser at 100 MS/s on the pad | Frequency within the 0.2 % bound `lab_pwm_plan()` states (worst 997 ppm at 39 960 Hz in the host sweep); duty within one 10-bit tick of the request (±1 point); 100 % shows the documented one low tick per 1 024; Stop leaves the pad hi-Z. One PWM at a time: a second pin is refused as busy. Record every reading | | |
| 13 | Pulse widths and jitter | Pulse mode on pin 38, each of 10 µs, 100 µs, 1 ms, 10 ms, 100 ms, 1 s, both polarities, 20 repeats of the shortest three; analyser at 100 MS/s | Width within ±10 % or ±20 µs, whichever is larger (a V1 acceptance threshold of this procedure, not a firmware promise: the firmware's contract is only the six widths); the idle level is restored; one pulse per key press; BACK during the 1 s pulse ends it at once and the pad ends at the opposite level | | |
| 14 | A pad that follows PWM from outside | A PWM running into a pad also driven by the generator through 1 kΩ | Documented limit confirmed: PWM is not read back, so nothing is detected (ADR-0023 0702 addendum 7); record what the pad does and that the confirmation's probe was the only guard. Not a pass of detection | | |
| 15 | Frequency accuracy against the generator | Generator into pad 39 through 330 Ω; GPIO app, pin 39 input, Measure at 1 Hz, 10 Hz, 100 Hz, 1 kHz, 10 kHz, 25 kHz and 50 kHz (50 % duty); the generator's frequency checked on the scope | Reading within ±0.5 % up to 10 kHz and ±2 % at 25 and 50 kHz of the reference; 5 runs at each, all recorded; the same results with the other edge polarity of the generator | | |
| 16 | Duty accuracy and the edge latency asymmetry | Generator at 100 Hz, 1 kHz, 10 kHz, 25 kHz and 50 kHz at duty 10, 25, 50, 75 and 90 % into pad 39; scope for the reference | Duty within ±2 points up to 10 kHz and ±5 points at 25 and 50 kHz. The rising-to-falling latency asymmetry of the interrupt is `(measured duty - reference duty) x period` at 50 kHz and recorded in ns (±5 points at 50 kHz is ±1 µs); a systematic bias that exceeds the limit is FAIL | | |
| 17 | The plan's PWM-into-RX loop, restated with independent references | Not a wire between the two pins. Close the loop in two halves with two instruments: the device's own PWM at 1 kHz, 10 kHz and 40 kHz read on the logic analyser (check 12), and the generator at the same frequencies and duties read by the device (checks 15 and 16); then the analyser's own reading of the generator against the device's | The two halves agree: at each frequency the device's PWM as the analyser sees it, and the generator as the device sees it, are within the sum of the two tolerances above. Record both columns. (A second device, or the lab header, could close the loop with a wire; neither is needed.) | | |
| 18 | Range limits, a fast clock and a noisy edge | Generator at 60 kHz, 100 kHz, 1 MHz and 10 MHz (3.3 V); then 0.5 Hz; then a steady high and a steady low; then a tactile switch (bounce) and an RC-filtered edge (330 Ω into 1 nF) | Above 50.05 kHz the answer is `> 50 kHz, not measured` from the 20 ms gate with no capture, at 1 MHz and 10 MHz as well, **with no interrupt watchdog and no reset** (the handler switches itself off after a few thousand edges); 0.5 Hz is `< 1 Hz`; steady lines are `no signal (high)` or `(low)`; the bounce and the slow edge give a result flagged `glitches` or `invalid` or a measured number within tolerance, never an unflagged wrong one | | |
| 19 | A flash or card write during a window | Measure 1 kHz at 25 % twenty times while the log file is being written (the storage service draining) and once during a Settings write | Every result is within ±0.5 % and none says `overflow` or `invalid`: the IRAM handler's reason (ADR-0023 0703 addendum 1) held on the chip | | |
| 20 | Real devices at their addresses, and the hints | Three or more breakouts of ours at known addresses on the shared bus; I2C app, Scan | The list is exactly those addresses (plus 0x50 or 0x51 if a module is fitted and the PN532's 0x24 if present) and nothing else; the 112-probe sweep takes at most 600 ms; each hint says "may be" and names a part that plausibly sits at that address; record the sweep time | | |
| 21 | 100 and 400 kHz waveforms and pull-ups | Logic analyser and scope on `I2C_SDA` and `I2C_SCL` during a read at each speed | SCL within ±10 % of 100 and of 400 kHz; rise times within the I²C specification for the speed with the board's pull-ups and the bus capacitance (write both); ACK on the address; no stretching the firmware cannot follow | | |
| 22 | Register read, dump and a confirmed write on a real device | WHO_AM_I of the breakout read; a 256-byte dump behind its confirmation; a write of one byte to a harmless register behind its confirmation; BACK on each confirmation | WHO_AM_I equals the datasheet value; the dump asks first, BACK makes no bus traffic (analyser), a confirm makes 16 transactions of 16; the write changes exactly the register (read back) and logs `lab: i2c write ... ok`; a second write asks again | | |
| 23 | Refused accesses leave no traffic | With a module fitted and, separately, the NFC module fitted and confirmed: try Write to 0x50, a 16-bit register read of 0x50, and Read and Write on 0x24 | Each is refused with its alert (`module eeprom`, `nfc reader`), the analyser sees no start condition for them, and the module's descriptor reads byte-identical before and after | | |
| 24 | A device pulled mid-sweep, a stuck bus, recovery | Start a Scan and pull a device that holds SDA low; separately hold SDA low with a 1 kΩ to ground for a sweep; then release | The sweep ends with `Bus error` after at most three probes and the UI returns; the display, the buzzer and the NFC poll keep working; after the release a rescan lists everything again with no reset. Record whether recovery needed a power cycle (if it did, that is a FAIL and a task) | | |
| 25 | The SPI probe at each clock and mode with the panel and card running | Catalogue-34 breakout in slot 0; SPI app at 100 kHz, 1 MHz and 4 MHz, modes 0 to 3, 1, 8 and 32 bytes, while the panel redraws and a 100 kB card write runs; analyser on the module `MOD_CS`, SCLK, MOSI, MISO and on the panel and card chip selects | One transaction per confirmation (BACK makes none); SCLK within ±5 % of the request; the module's `MOD_CS` low only during the transfer; the panel's and card's chip selects never move because of the probe; no display corruption and the card file byte-identical afterwards. Record the worst panel stall | | |
| 26 | A real flash's JEDEC ID | Probe preset `9F 00 00 00` on a SPI NOR breakout | The reply is the part's manufacturer, type and capacity bytes from its datasheet; the lease (`spiprobe`) is claimed and released inside the transaction (Modules app) | | |
| 27 | Refusals on real modules | SPI app with the CC1101 module fitted, with slot 0 empty, and with slot 1 | `owned by Sub-GHz` with an alert and the Sub-GHz detection unaffected (`subghz: cc1101 version 0x14` again); `no module`; slot 1 has no chip select. On each the analyser sees nothing on `MOD_CS` | | |
| 28 | UART loopback: 115200, the baud limits and every format | TX wired to RX through the two 330 Ω; UART app Loopback test at 115200 8N1, then 1 200, 9 600, 230 400, 460 800 and 921 600 Bd, then 8E1, 8O1, 8N2 and 7E1 at 115200 | `loop 64/64 ok` at each; at 921 600 Bd record whether the edge-free loopback still reads 64/64 (it uses the UART driver, not the edge capture); the confirmation of TX comes first, once; both pins are released on leaving | | |
| 29 | A USB-serial adapter in both directions | Adapter TX to pad 39 and RX to pad 38 (3.3 V, grounds joined) at 115200 8N1 | Send (canned strings, 1 to 32 bytes) shows the exact bytes in the terminal, one per key press; text typed on the PC appears in the monitor in hex and in ASCII; a deliberate format mismatch (8E1 against 8N1) is counted as framing or parity errors and shown, never dropped silently | | |
| 30 | Auto-baud on a real stream | The adapter sends a steady text stream at 9 600, 19 200, 38 400, 57 600, 115 200 and 230 400 Bd | Each is named within 3 % and the Baud row follows; a stream with no clean short pulse reads `unknown`. Record the highest baud the edge capture still names (a bit at 921 600 Bd is 1.09 µs) | | |
| 31 | Glitches on the TX line | Scope on pad 38 while the UART app opens, runs a send, changes the format with TX on (the driver is installed again, which routes the pad once), and closes | The line idles high after the confirmation and low-to-high only as a send; the format change glitch (if any) is measured and written here; on close the pad is hi-Z. A glitch a wired receiver would read as a byte is recorded, with the receiver's reaction | | |
| 32 | A long log to a real card, and a pulled card | Monitor with Log on, the adapter streaming at 115200 at full rate for 10 minutes; then pull the card during a second log | The file holds exactly the bytes the sender counted (the app prints the CRC-32 on close: compare it with the PC's), no overrun at 115200 (an overrun is counted and shown if it happens); at 1 MiB the log stops and says so; a pulled card ends the log with a message, no crash, the partial file is still valid, both pins are released | | |
| 33 | The automated self-test checks on silicon | Self-Test, Run all, with a card and a module fitted, then without a card | RAM, flash (two SHA-256 passes agree and the chip size matches the image header), settings, storage (4 kB written, read back, deleted), module bus (agrees with address probes of 0x50 and 0x51) and events and stacks all `pass`; without a card Storage is `skip`; the free heap never falls below 128 kB; record each check's time | | |
| 34 | Each attested check really observed | The walk: the display's colour bars, all six buttons, the buzzer, the status LED, each answered by a person looking and listening | Display colours correct (red is red), every key seen, the buzzer heard, the LED seen, each recorded `attested` and never `pass`; answering "no" records `fail` and ends the walk; write what was seen next to each | | |
| 35 | The report from a real card | Export report twice, then read both files on a PC; pull the card during a third export | `report_NNNN.txt` and the next number exist, at most 16 kB, no control characters, with version, board, uptime, the `stark_diag` snapshot, tasks, module table, storage state, every result with its evidence and the log tail; the pulled-card export leaves no partial file (atomic); nothing is sent anywhere | | |
| 36 | Stack and heap on silicon after all the tools | A 10-minute session through every lab app (GPIO with PWM and a measurement, I²C, SPI, UART with a log, Self-Test), then the diagnostics app | The app worker's stack peak is well under its 4 096 B (the V0.7 emulator peak is in docs/measurements.md); the heap returns to its boot value after the last app stops; no `diag:` event drops | | |

## Pass rules

* Checks 4, 7 and 12-14 drive a pad. They are attempted only after checks 1-3 and 10 have
  passed, with a series resistor or a known load on the pad (never a direct short), and with
  the instrument already connected.
* A pin that is driven or loaded outside what this document names, a source above 3.6 V, a
  reserved pad that moves because of a lab tool, a contention that is not detected and
  released, or a write that reaches a module ID EEPROM or the PN532 stops the bring-up and is
  a defect, not a result to record and move past.
* Results also go into `docs/measurements.md` -> "Hardware (gated, V1 prototype)", lab-tool
  rows, and every FAIL is a task in TASKS.md before V1 closes. The thresholds in checks 13 and
  the other rows that cite "a V1 acceptance threshold" are this procedure's, written down
  before any measurement; the measured value is recorded whatever it is.
