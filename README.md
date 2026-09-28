# STARK ONE

A modular, portable embedded electronics & security-lab device.

STARK ONE is an ESP32-S3 based handheld lab tool: a colour TFT UI, a 6-key navigation
cluster, and a growing set of **lab applications** (GPIO, I²C, UART, IR, NFC, Sub-GHz)
built on a clean, extensible firmware architecture. It is inspired by the class of
portable multi-tools such as the Flipper Zero, but it is **not a clone**: own firmware
architecture, own module standard, own PCB, own enclosure.

> **Status: V0.1 complete** (STARK-0100 … STARK-0110, on V0: STARK-0001 … 0021). The
> firmware boots on the ESP32-S3 N16R8 in Espressif's free esp-emulator: event bus,
> debounced six-key input with an OK+BACK reservation and long-BACK home, band-rendered
> UI with multi-rect damage and frame statistics, a modal confirm/alert dialog, a
> launcher with category headers and icons, Latin-1/Turkish text in two fonts with
> wrapping, an app worker helper with fault containment, `stark_diag`, and seven apps
> (About, Diagnostics, App Test, Buzzer Test, Display Test, Input Test, Hello). CI runs
> the host suite (with a host UI test port), the pinned build and the emulator
> scenarios including a 60 s soak — all free ([docs/VALIDATION.md](docs/VALIDATION.md)).
> Evidence: [docs/measurements.md](docs/measurements.md). Next: V0.2 (storage &
> settings), once it is specified; the performance gates live at the V1 hardware
> prototype.

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
