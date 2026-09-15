# STARK ONE

A modular, portable embedded electronics & security-lab device.

STARK ONE is an ESP32-S3 based handheld lab tool: a colour TFT UI, a 6-key navigation
cluster, and a growing set of **lab applications** (GPIO, I²C, UART, IR, NFC, Sub-GHz)
built on a clean, extensible firmware architecture. It is inspired by the class of
portable multi-tools such as the Flipper Zero, but it is **not a clone**: own firmware
architecture, own module standard, own PCB, own enclosure.

> **Status: planning / pre-implementation.** No firmware code exists yet.
> This repository currently contains the engineering plan only.

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
* **Simulation:** Wokwi, running the *same* build artifact as real hardware
* **First milestone (V0):** boot → display → 6 buttons → menu → buzzer, in Wokwi

## Where to start

Read [PROJECT.md](PROJECT.md), then [ARCHITECTURE.md](ARCHITECTURE.md), then pick up
**STARK-0001** from [TASKS.md](TASKS.md).

## Use policy

STARK ONE is developed for use on hardware we own, our own RF/NFC/IR test rigs,
lab environments, CTF/educational systems, and systems we are explicitly authorised to
test. See [docs/SECURITY_SCOPE.md](docs/SECURITY_SCOPE.md) — it is binding on feature
design, not a disclaimer.
