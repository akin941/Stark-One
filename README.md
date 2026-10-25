# STARK ONE

A modular, portable embedded electronics & security-lab device.

STARK ONE is an ESP32-S3 based handheld lab tool: a colour TFT UI, a 6-key navigation
cluster, and a growing set of **lab applications** (GPIO, I²C, UART, IR, NFC, Sub-GHz)
built on a clean, extensible firmware architecture. It is inspired by the class of
portable multi-tools such as the Flipper Zero, but it is **not a clone**: own firmware
architecture, own module standard, own PCB, own enclosure.

> **Status: V0.4 complete (software/protocol-verified; real tags and RF at V1)**
> (STARK-0401 … 0405, ADR-0020; V0.3: STARK-0301 … 0307, ADR-0019; V0.2: STARK-0201 …
> 0207, ADR-0018; V0.1: STARK-0100 … 0110; V0: STARK-0001 … 0021). On top of V0.3's
> infrared lab, V0.4 adds:
> - an NFC stack for a PN532 on the shared I²C bus: NDEF encode/decode, a PN532 protocol
>   driver over an injected transport, and tag detection and identification (NTAG213/215/216,
>   Ultralight; MIFARE Classic and ISO-DEP identified only);
> - Type 2 NDEF reads, and confirmed NDEF writes to our own tags that never lock them;
> - dumps to the card in a documented text format;
> - an NFC app that turns the RF field on only while it is open.
>
> Eleven apps. Validation is free and local: host tests with spec-derived vectors, the
> production driver run against a **test-only fake PN532** at its transport seam, the
> pinned ESP-IDF build, and the firmware in Espressif's esp-emulator (whose I²C bus is
> empty). Neither the fake nor the emulator reads a real tag. The PN532 on the bus, the
> antenna, range and real tags are V1 hardware gates (`docs/bringup/v1-nfc.md`), as are
> the IR front-end (`docs/bringup/v1-ir.md`) and the real microSD card
> (docs/measurements.md).

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
