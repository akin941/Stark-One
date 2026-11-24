# STARK ONE

A modular, portable embedded electronics & security-lab device.

STARK ONE is an ESP32-S3 based handheld lab tool: a colour TFT UI, a 6-key navigation
cluster, and a growing set of **lab applications** (GPIO, I²C, SPI, UART, IR, NFC, Sub-GHz)
built on a clean, extensible firmware architecture. It is inspired by the class of
portable multi-tools such as the Flipper Zero, but it is **not a clone**: own firmware
architecture, own module standard, own PCB, own enclosure.

> **Status: V0.6 complete (software/RF-protocol-verified; no radio has transmitted or received, physical RF at V1)**
> (STARK-0601 … 0606, ADR-0022; V0.5: STARK-0501 … 0505, ADR-0021; V0.4: STARK-0401 … 0405,
> ADR-0020; V0.3: STARK-0301 … 0307, ADR-0019; V0.2: STARK-0201 … 0207, ADR-0018; V0.1:
> STARK-0100 … 0110; V0: STARK-0001 … 0021). On top of V0.5's module bus, V0.6 adds a
> Sub-GHz laboratory module for a CC1101 in slot 0:
> - an RSSI meter and a band scan of 433.05–434.79 MHz, and OOK / 2-FSK capture to a documented
>   text format on the card, with a timing histogram;
> - generic pulse-width, pulse-distance and Manchester decoders for display;
> - replay of **our own** saved captures only, behind a confirmation naming the file and the
>   duty left, with fixed limits no setting can change: 0 dBm `PATABLE`, 10 % per rolling hour
>   plus a 9x off-time, 2 s per transmission, one transmission per confirmation.
>
> Thirteen apps. Validation is free and local: host tests with spec-derived vectors, the
> production service run against a **test-only fake CC1101** at its transport seam, the pinned
> ESP-IDF build, and the firmware in Espressif's esp-emulator (which has no radio). Neither the
> fake nor the emulator has a chip, a crystal or an antenna. The CC1101 on the bus, the RMT data
> paths, RSSI, timing of a real remote, power and on-air time are V1 hardware gates
> (`docs/bringup/v1-subghz.md`), as are the PN532 and antenna (`docs/bringup/v1-nfc.md`), the
> module connector (`docs/bringup/v1-modules.md`), the IR front-end (`docs/bringup/v1-ir.md`)
> and the real microSD card (docs/measurements.md).

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
