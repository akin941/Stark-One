# STARK ONE

A modular, portable embedded electronics & security-lab device.

STARK ONE is an ESP32-S3 based handheld lab tool: a colour TFT UI, a 6-key navigation
cluster, and a growing set of **lab applications** (GPIO, I²C, SPI, UART, IR, NFC, Sub-GHz) and a self-test with a report to the card
built on a clean, extensible firmware architecture. It is inspired by the class of
portable multi-tools such as the Flipper Zero, but it is **not a clone**: own firmware
architecture, own module standard, own PCB, own enclosure.

> **Status: V0.7 complete (software / lab-protocol-verified; no pad has been driven, no wire measured and no real peripheral has answered on real hardware, physical lab-tool checks at V1)**
> (STARK-0701 … 0708, ADR-0023; V0.6: STARK-0601 … 0606, ADR-0022; V0.5: STARK-0501 … 0505, ADR-0021;
> V0.4: STARK-0401 … 0405, ADR-0020; V0.3: STARK-0301 … 0307, ADR-0019; V0.2: STARK-0201 … 0207,
> ADR-0018; V0.1: STARK-0100 … 0110; V0: STARK-0001 … 0021). On top of V0.6's Sub-GHz module, V0.7
> adds the everyday bench tools, behind one pin-safety service:
> - **GPIO**: read, drive, toggle, pulse, pull and PWM out on the pins `stark_board` offers (the two
>   lab UART pins, and an empty module slot's three), with weakest drive strength, a confirmation
>   before any drive, a read-back that releases a contended pin, and frequency and duty measured
>   from timestamped edges (1 Hz to 50 kHz);
> - **I2C**: an address-only scanner with known-device hints, register read, a confirmed dump and
>   confirmed writes, and refusals for the module ID EEPROMs and the NFC reader;
> - **SPI**: a one-transaction probe on a leased module slot, refused on any slot a driver owns;
> - **UART**: a hex/ASCII monitor, send, a 64-byte loopback test, auto-baud and logging to the card;
> - **Self-Test**: automated RAM, flash, settings, storage, module-bus and event checks, four
>   operator-attested checks recorded as `attested` and never `pass`, and a bounded report to the card.
>
> Eighteen apps. Validation is free and local: host tests with spec-derived vectors from an
> independent Python reference, the production services run against a **test-only pad, bus, SPI and
> UART model** at one transport seam, the pinned ESP-IDF build, and the firmware in Espressif's
> esp-emulator. Neither the model nor the emulator has a pad that moves, a drive strength, a clock edge
> or a real device. The offered pins on the board, drive and contention, PWM and measurement accuracy
> against a reference, real I²C and SPI parts, the UART against an adapter and the self-test on silicon
> are V1 hardware gates (`docs/bringup/v1-labtools.md`, ROADMAP V1 exit 13), alongside the Sub-GHz module
> (`docs/bringup/v1-subghz.md`), the PN532 and antenna (`docs/bringup/v1-nfc.md`), the module connector
> (`docs/bringup/v1-modules.md`), the IR front-end (`docs/bringup/v1-ir.md`) and the real microSD card
> (docs/measurements.md). Earlier milestones: V0.6 added the Sub-GHz module (RSSI meter, band scan,
> capture, decoders, and replay of our own captures behind fixed transmit limits); V0.5 the module bus;
> V0.4 NFC; V0.3 infrared; V0.2 storage and settings.

## Documents

| Document | Purpose |
| --- | --- |
| [PROJECT.md](PROJECT.md) | Product definition, scope, goals, non-goals, glossary |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Firmware layering, components, APIs, threading & memory model |
| [HARDWARE.md](HARDWARE.md) | Target hardware, pin map, electrical constraints, BOM, PCB path |
| [WOKWI.md](WOKWI.md) | Simulation as a first-class dev environment: setup, `diagram.json`, limits |
| [ROADMAP.md](ROADMAP.md) | Milestones V0 → V2 with entry/exit criteria |
| [TASKS.md](TASKS.md) | Implementation backlog — small, testable, dependency-ordered tasks |
| [DECISIONS.md](DECISIONS.md) | Architecture Decision Records (ADR-0001 …) with rejected alternatives |
| [TESTING.md](TESTING.md) | Test strategy, quality gates, CI pipeline definition |
| [docs/SECURITY_SCOPE.md](docs/SECURITY_SCOPE.md) | Authorised-use policy: what this device will and will not do |
| [AGENTS.md](AGENTS.md) | Working agreement for coding agents (OpenCode) and humans |

## Quick orientation

* **MCU:** ESP32-S3 (DevKitC-1 for V0, ESP32-S3-WROOM-1 N16R8 on the custom PCB)
* **Framework:** ESP-IDF v6.1 (pinned), C11, CMake
* **Display:** ILI9341 2.8" 320×240 SPI, driven through `esp_lcd`
* **UI:** in-house `stark_gfx` + `stark_ui` (LVGL deliberately deferred — see ADR-0005)
* **Validation:** host tests + Espressif's esp-emulator running the *same* build
  artifact as real hardware, locally and in CI; Wokwi optional (ADR-0017)
* **First milestone (V0):** boot → display → 6 buttons → menu → buzzer, in Wokwi

## Where to start

Read [PROJECT.md](PROJECT.md), then [ARCHITECTURE.md](ARCHITECTURE.md), then
[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) to build, test and simulate. New work starts
from the next unfinished task in [TASKS.md](TASKS.md).

## Use policy

STARK ONE is developed for use on hardware we own, our own RF/NFC/IR test rigs,
lab environments, CTF/educational systems, and systems we are explicitly authorised to
test. See [docs/SECURITY_SCOPE.md](docs/SECURITY_SCOPE.md) — it is binding on feature
design, not a disclaimer.
