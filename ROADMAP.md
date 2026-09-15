# ROADMAP — STARK ONE

Each milestone has an **entry condition**, a **scope**, an **exit criterion** and an
explicit **out-of-scope** list. A milestone is finished when its exit criteria are
demonstrably met in CI (or on the bring-up checklist for hardware milestones) — not
when the code "mostly works".

Ordering rationale is at the end (§sequencing notes); two deviations from the
originally proposed order are argued there.

---

## V0 — Wokwi core prototype

**Goal:** a device that boots, draws, reacts to six keys, beeps, and shows a working
menu — entirely in simulation.

**Entry:** empty repository.

**Scope**

* Repository, toolchain pinning, reproducible build (Docker + local ESP-IDF v6.1)
* CI: build, format check, layer check, host tests, Wokwi smoke scenario
* `stark_err`, `stark_log`, `stark_board`, `stark_hal`
* `stark_event` (core + port), host-tested
* `stark_gfx` (surfaces, primitives, 1bpp font, text), host-tested
* `stark_display` (ILI9341 via `esp_lcd`, band rendering)
* `stark_input` (debounce/repeat core + 5 ms sampling service), host-tested
* `stark_ui` (status bar, screen stack, list menu)
* `stark_app` (explicit static registry, lifecycle, launch/stop — ADR-0009)
* `stark_buzzer` (LEDC tones, UI feedback)
* Status LED heartbeat
* Four trivial apps: About, Input Test, Display Test, Buzzer Test
* `diagram.json`, `wokwi.toml`, one CI scenario

**Exit criteria** — functional and deterministic only; performance is gated at
V1 prototype (ADR-0011).

1. `idf.py build` clean, warnings-as-errors, on a pinned toolchain in CI.
2. Wokwi scenario `v0-boot-and-menu` passes: boot → navigate → launch app → BACK.
3. Host test suite green, ≥ 80 % line coverage over `stark_event`, `stark_gfx`,
   `stark_input_core`, `stark_ui_model`.
4. `boot: ui_ready in <N> ms` is logged, in the right order relative to the other boot
   lines. The value of `<N>` is recorded as informational, **not asserted**.
5. Rendering is correct: no seams at band boundaries, partial updates touch only the
   requested region, a full-screen redraw is visually identical to a partial one.
   Frame rate is logged, not gated.
6. ≥ 200 kB internal heap free at the root menu, and heap after launching/exiting each
   app ten times is within 1 kB of baseline. (Memory is deterministic — it stays a
   simulation gate.)
7. Zero dropped events and zero render errors over the full scenario set.
8. No `#ifdef` referencing the simulator anywhere in the tree.

**Out of scope:** storage, settings, radios, power, LVGL, PSRAM use in firmware,
capability gating / module discovery, Wi-Fi, USB classes.

**Note on the simulated SKU:** the Wokwi board is configured as the physical target
(16 MB flash, 8 MB octal PSRAM) while firmware keeps PSRAM disabled — V0 must be shown
to run on the production SKU without using PSRAM (ADR-0014).

---

## V0.1 — display / input / application architecture hardening

**Goal:** turn the V0 skeleton into something a dozen apps can be built on without
touching core code.

**Scope**

* Damage-tracking refinement: per-widget invalidation, coalescing, frame-overrun stats
* Screen stack semantics: modal dialogs, confirm/alert helper, long-BACK → root
* Menu improvements: categories, icons, paging (a generic "disabled item" *rendering*
  state is fine here; what disables an item — capability bits — arrives at V0.5)
* Text: UTF-8 with Latin-1/Turkish glyph coverage, a second font size, text wrapping
* Input: key-repeat tuning, chord reservation (OK+BACK) for a future soft-reset
* App lifecycle: worker-task helper, `on_stop` join contract, crash containment
  (an app returning an error unwinds to the root menu instead of panicking)
* `stark_diag` v1: heap, task high-water marks, FPS, event drops — surfaced in an app
* Theme/colour constants centralised

**Exit criteria**

1. A new "hello" app can be added in a single new component directory, < 120 lines,
   with no edits to `components/`.
2. Modal confirm dialog works and is covered by a Wokwi scenario.
3. Event-drop counters are zero over a 60 s scenario. Frame-overrun counts are
   *reported* from simulation but gated only on hardware (ADR-0011).
4. Diagnostics app displays live heap and FPS.

---

## V0.2 — storage & settings

**Goal:** the device remembers things and can write captures.

**Scope**

* `stark_storage`: microSD over shared SPI2 + FATFS, mount/unmount, hot-insert events,
  bus arbitration with the display, safe-eject semantics
* Path conventions: `/sd/stark/{captures,logs,apps,config}`
* `stark_settings`: typed key/value over NVS, defaults, validation, change events
* Settings app: brightness, volume, key repeat, log level, "restore defaults"
* Log-to-file sink for `stark_log` (bounded, rotating)
* Wokwi: `sim/diagram.storage.json` with `wokwi-microsd-card`

**Exit criteria**

1. Settings survive reboot (verified in a Wokwi scenario: set → reboot → assert).
2. A 100 kB file writes and reads back byte-identical while the UI keeps rendering.
3. Removing the card mid-write degrades gracefully (error surfaced, no panic).
4. Mount failure leaves the device fully usable.

---

## V0.3 — infrared lab

**Goal:** receive, decode, store and replay IR from **our own** devices.

**Scope**

* `stark_ir` driver on the ESP-IDF v6 RMT driver (`rmt_rx` / `rmt_tx` + carrier)
* Pure codec layer (host-tested against recorded vectors): NEC, NEC-extended, RC5,
  RC6, Sony SIRC, plus a raw timing capture/replay format
* IR app: live receive view (protocol, address, command, raw timings), save to SD,
  browse saved signals, transmit a saved signal
* Universal-remote-style brute lists are **out of scope** (SECURITY_SCOPE.md)

**Verification split** (Wokwi models RMT TX only, LED-strip oriented — WOKWI.md §5):

* Simulated: app navigation, file I/O, UI. Codecs: host tests.
* Hardware: TSOP38238 receive from our own remotes; transmit verified with a second
  receiver and with the target device responding.

**Exit criteria**

1. Codec host tests pass on ≥ 20 recorded vectors per supported protocol.
2. On hardware: capture from an own remote, save, replay, device responds.
3. Raw capture/replay works for an unsupported protocol.

---

## V0.4 — NFC lab

**Goal:** read and inspect 13.56 MHz tags we own; write NDEF to our own tags.

**Scope**

* `stark_nfc` driver for a PN532-class module over I²C (SPI fallback)
* Tag detection, UID read, ATQA/SAK reporting, technology identification
* NDEF parse/serialise (host-tested), record viewer, write NDEF to writable tags
* Tag info app; dump to SD in a documented, non-proprietary format

**Explicitly excluded by policy:** key-recovery attacks (nested/darkside/hardnested),
default-key brute forcing against tags we do not own, UID spoofing for access systems,
cloning of credentials. See [docs/SECURITY_SCOPE.md](docs/SECURITY_SCOPE.md).

**Verification:** hardware only (no Wokwi part) + host tests for NDEF and framing.

**Exit criteria**

1. Reads UID and technology of our own NTAG/ISO14443A tags reliably (≥ 20/20 reads).
2. Writes and reads back an NDEF URI record on a writable tag.
3. Module absence is detected and the app disables itself cleanly.

---

## V0.5 — expansion / module bus

**Goal:** hardware modules become discoverable rather than hard-coded.

**Scope**

* Module connector pinout frozen ([HARDWARE.md §6](HARDWARE.md))
* Module ID EEPROM descriptor format (ID, revision, capability bits, name)
* `stark_module`: enumerate at boot + on hotplug, publish capabilities, arbitrate
  shared SPI CS / IRQ lines
* **Introduction of capability bits and the `stark_app_t.caps_required` field** — this
  milestone owns them end to end; nothing of the sort exists before it (ADR-0009)
* Capability-gated app listing (apps grey out when their module is absent)
* Retrofit IR and NFC as modules behind this interface
* Module inspector app

**Exit criteria**

1. Plugging a module changes the app list without a firmware change.
2. Unknown/absent EEPROM yields a "generic module" entry, never a boot failure.
3. Two modules can share the SPI bus without corrupting the display.

---

## V0.6 — Sub-GHz laboratory module

**Goal:** observe and characterise our own 433 MHz devices.

**Scope**

* CC1101-class module driver (SPI + GDO0/GDO2), register profiles for common data rates
* Receive-first feature set: RSSI meter, frequency scan across the SRD band, OOK/2-FSK
  bitstream capture, raw capture to SD, timing/histogram analysis view
* Decoders for **generic, non-security** OOK protocols (e.g. simple fixed-pattern
  sensors) implemented as pure host-tested codecs
* Transmit: limited to replaying **our own** recorded signals from our own test
  transmitters, behind an explicit confirmation, hard duty-cycle limiting and a
  firmware power cap ([HARDWARE.md §7](HARDWARE.md))

**Explicitly excluded by policy:** rolling-code attacks, jamming, de-authentication,
capture-replay against access control, brute-force transmission.

**Verification:** hardware only + host-tested codecs; optional custom Wokwi chip stub
for register-sequence sanity.

**Exit criteria**

1. RSSI/scan view shows our own transmitter and tracks distance sensibly.
2. Raw capture of an own device's transmission reproduces its documented timing.
3. Replay of our own capture triggers our own receiver.
4. TX is impossible without an explicit confirmation and respects the duty-cycle cap.

---

## V0.7 — diagnostics & GPIO/I²C/UART lab tools

**Goal:** the everyday bench tools — the apps that will get the most real use.

**Scope**

* GPIO app: per-pin read/write/toggle/pulse, pull configuration, PWM out, frequency and
  duty measurement (PCNT/MCPWM-free implementation), safe-pin allowlist
* I²C scanner: bus scan, address list, register read/write, known-device hints
* SPI probe: simple transaction sender/inspector
* UART tools: bridge, monitor, auto-baud detection, hex/ASCII view, logging to SD
* Self-test/diagnostics app: RAM, flash, SD, display, buttons, buzzer, module bus
* Pin-safety guardrails: reserved pins are not offered; output drive is confirmed

**Verification:** largely simulatable — I²C master, UART, GPIO and PWM are modelled by
Wokwi. `sim/diagram.i2c.json` gains I²C peripherals for the scanner scenario.

**Exit criteria**

1. I²C scanner finds simulated devices at the expected addresses in a Wokwi scenario.
2. UART bridge round-trips data at 115200 in simulation.
3. GPIO app cannot configure a reserved pin.

---

## V1 prototype — physical breadboard

**Goal:** everything validated in simulation runs on real silicon.

**Scope:** breadboard assembly, hardware bring-up checklist, SPI timing at 20/40 MHz,
display panel configuration (rotation/mirror/BGR), real button bounce, buzzer volume,
SD reliability, IR/NFC/Sub-GHz module bring-up, thermal and current measurements,
performance profiling on hardware.

**Exit criteria** — this milestone owns **all performance gates** deferred from the
simulated milestones (ADR-0011):

1. Every V0–V0.7 functional exit criterion re-verified on hardware where applicable.
2. **`boot: ui_ready` ≤ 600 ms** from reset, first pixel ≤ 400 ms, measured on hardware.
3. **Root menu sustains ≥ 20 FPS**; full-screen refresh ≤ 60 ms at the chosen SPI clock.
4. Key-to-pixel latency ≤ 50 ms for a selection move.
5. No frame overruns during a 60 s navigation session.
6. Measured boot time, refresh time, FPS, latency, free heap, binary size and current
   draw recorded in `docs/measurements.md`, alongside the simulated figures for
   reference (the delta between the two is itself useful data).
7. The pin map survives unchanged, or the changes are merged into `stark_board` and
   `diagram.json` together.

---

## V1 hardware — custom PCB

**Scope:** KiCad schematic + 4-layer layout, power tree (USB-C, Li-ion charge,
protection, fuel gauge), ESD, panel and SD connectors, module connector, test points,
DFM review, fabrication, assembly, bring-up per [HARDWARE.md §8](HARDWARE.md).

**Exit criteria:** two assembled boards pass the full bring-up checklist; firmware runs
unmodified except for `STARK_BOARD_PCB_R1`; errata documented.

---

## V1 enclosure — mechanical

**Scope:** 3D-printed clamshell, keypad, light pipe, module bay, port cutouts, fit and
ergonomics iteration; mechanical drawings exported from KiCad first.

**Exit criteria:** device is usable one-handed, buttons have positive feel, no rattle,
assembly takes < 10 minutes with documented steps.

---

## V2 — advanced modules & power optimisation

**Candidate scope (re-prioritised at entry):** power management (backlight dimming,
light sleep between inputs, battery SoC and runtime estimate), a second module type,
LVGL re-evaluation (ADR-0005), scripting or macro support, OTA/USB firmware update,
capture file format tooling on the host, and any V0.x item deferred for time.

---

## Sequencing notes (deviations and rationale)

1. **Storage (V0.2) before the radios.** Every radio milestone wants to save captures
   and settings. Doing storage first removes a dependency from three later milestones
   and forces the SPI-bus-sharing problem to be solved once, early, while the only
   other bus user is the display. This matches the requested order.
2. **The module bus (V0.5) lands after IR and NFC, not before.** Designing a module
   abstraction before two concrete modules exist would be guesswork; retrofitting two
   known drivers into the abstraction produces a standard that actually fits. The cost
   is one refactor, which is cheaper than the wrong connector standard.
3. **Sub-GHz (V0.6) after the module bus** because it is the first module we will build
   ourselves rather than buy, and it should be the first consumer of a finished
   standard.
4. **Diagnostics/GPIO tools (V0.7) last among the V0.x line, despite being the most
   simulatable.** Argument for moving it earlier: it needs no new hardware and would
   deliver daily-use value sooner. Argument for keeping it last, which wins here: it is
   the milestone that most wants storage, settings, capability gating and a mature UI —
   building it last means building it once. If schedule pressure appears, promoting the
   I²C scanner alone to V0.2 is a cheap, low-risk exception.
5. **Power management deferred to V2.** Optimising power before the real load profile
   (backlight, radios, SD) exists would be measuring noise.
