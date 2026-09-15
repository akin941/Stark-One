# PROJECT — STARK ONE

## 1. What we are building

STARK ONE is a battery-powered, handheld, modular embedded **electronics and security
engineering lab tool**. One device, one UI, many applications: a signal generator here,
an I²C scanner there, an IR remote analyser, an NFC tag reader, a Sub-GHz receiver.

The product thesis: the value is not in any single radio — it is in the **platform**.
A clean firmware architecture where a new lab tool is a self-contained application
module of a few hundred lines, and a hardware architecture where a new radio is a
plug-in module on a documented expansion bus.

STARK ONE takes inspiration from the Flipper Zero class of devices. It is not a clone,
and it shares no firmware, protocol database, or hardware design with it. We build our
own firmware architecture, our own module standard, our own PCB and our own enclosure.

## 2. Goals

**G1 — Platform before features.** The first milestone ships *no* lab application of
substance. It ships a boot path, a display pipeline, an input pipeline, an event bus,
an application manager and a menu. Every later milestone is then additive.

**G2 — Simulation-first development.** Wokwi is a first-class development target, not
a toy. The simulator runs the same build artifact as real hardware. There is no
`#ifdef WOKWI` in application or driver logic (see ADR-0010).

**G3 — Testability at every layer.** Pure logic (event bus, input state machine, menu
model, graphics primitives, protocol codecs) is host-testable with no ESP-IDF headers
in scope. Integration is verified in Wokwi in CI. Hardware behaviour is verified with a
written bring-up checklist.

**G4 — A real product path.** Breadboard → custom PCB → enclosure. Pin assignments,
power budget and connector standard are decided early enough to survive the transition,
and firmware carries a board-support layer from day one so the transition costs one
file, not a rewrite.

**G5 — Agent-executable plan.** Every task is small, independent, dependency-declared
and has acceptance criteria that a coding agent can check without asking a human.

**G6 — Lawful, authorised engineering use.** Feature design is bounded by
[docs/SECURITY_SCOPE.md](docs/SECURITY_SCOPE.md).

## 3. Non-goals

| Non-goal | Why |
| --- | --- |
| Flipper Zero firmware/app compatibility | Different architecture; compatibility would dictate our design |
| Bypassing protected access systems | Out of scope by policy — see SECURITY_SCOPE.md |
| Cloning credentials we do not own | Same |
| A general-purpose OS / dynamically loaded binary apps | Apps are compile-time registered modules; dynamic loading is a V2 question at best |
| Wi-Fi/BLE offensive tooling | Not in the roadmap; the radio stack is reserved for provisioning/telemetry if ever needed |
| Touch UI | The 6-key cluster is the interaction model; touch adds cost, code and no lab value |
| Ultra-low-power sleep engineering before V1 | Power work is scheduled at V2 once the load profile is real |

## 4. Users and use cases

Primary user: the author — an engineer working on their own embedded hardware.

Representative jobs-to-be-done:

1. "Is this I²C sensor alive, and at what address?" → I²C scanner app.
2. "What is this board printing on its debug UART at an unknown baud?" → UART tools.
3. "Why is this line not toggling?" → GPIO read/write/pulse + logic-level check.
4. "Does my own IR remote actually send what I think?" → IR receive/decode/replay of our own devices.
5. "Is my own 433 MHz sensor transmitting?" → Sub-GHz receive & spectrum/RSSI view.
6. "Does my NFC tag hold the NDEF record I wrote?" → NFC read of our own tags.

## 5. Product shape (V1 target)

* 2.8" 320×240 colour TFT, portrait-or-landscape UI, list-driven navigation.
* 6 keys: UP / DOWN / LEFT / RIGHT / OK / BACK. No touch, no encoder.
* Piezo buzzer for UI feedback and simple audio diagnostics.
* Status LED.
* microSD for captures, logs, and app data.
* USB-C for power, flashing and serial console.
* Li-ion cell + charger + fuel gauge (V1 hardware).
* One documented **STARK module bus** connector for plug-in radios (IR, NFC, Sub-GHz)
  and a breakout for lab GPIO/I²C/SPI/UART work.

## 6. Scope boundary of the first milestone (V0)

In: ESP32-S3 boot, logging, board support layer, ILI9341 bring-up, graphics
abstraction, 6-button input with debounce/repeat, event bus, screen stack, list menu,
application manager, buzzer feedback, status LED, Wokwi simulation + CI smoke test,
host unit tests.

Out: storage, settings persistence, IR, NFC, Sub-GHz, GPIO/I²C/UART lab apps, power
management, LVGL, Wi-Fi, BLE, USB device classes, OTA, PSRAM *use* by firmware
(the simulated board still models the 8 MB octal SKU — ADR-0014), and the entire
module-discovery/capability-gating system (capability bits, `caps_required`, I²C ID
EEPROM), which belongs to V0.5 (ADR-0009).

Explicitly: **do not implement systems V0 does not use.** Keep the architecture
extensible, but leave the extension points empty until a milestone needs them.

## 7. Constraints

* **Toolchain:** ESP-IDF v6.1, pinned; reproducible via the official Docker image.
* **Language:** C11 for firmware. C++ is permitted only if a third-party component
  forces it (see ADR-0012).
* **Host OS of the maintainer:** macOS (Apple Silicon). CI is Linux/Docker. Anything
  that only works on Linux must live in Docker.
* **Budget discipline:** V0/V0.1 must run on plain internal SRAM. No PSRAM dependency
  (see ADR-0014) so the design stays valid on smaller modules.
* **No secrets in the repository.** Wokwi CI tokens, Wi-Fi credentials, keys: CI
  secrets or untracked local files only.

## 8. Development model

| Role | Owner |
| --- | --- |
| Architecture, planning, review | Claude Code |
| Implementation | OpenCode (primary coding agent) |
| Virtual hardware validation | Wokwi (interactive + CI) |
| Physical validation | Breadboard, then custom PCB |
| PCB / mechanical design | KiCad, later |

Working agreement for agents: [AGENTS.md](AGENTS.md).

## 9. Definition of done (per task)

A task is done when **all** of the following hold:

1. `idf.py build` is clean with warnings-as-errors.
2. Host unit tests pass (`scripts/test_host.sh`) where the task defines any.
3. The Wokwi scenario for the task passes (`scripts/test_wokwi.sh`) where defined.
4. `clang-format` reports no diff.
5. The task's acceptance criteria in [TASKS.md](TASKS.md) are each demonstrably met.
6. Documentation touched by the change is updated in the same change.

## 10. Glossary

| Term | Meaning |
| --- | --- |
| **App** | A compile-time registered STARK application module implementing `stark_app_t` |
| **Board** | The physical target variant (wokwi/devkitc-1/stark-pcb-r1); selects a pin map |
| **Event bus** | The single publish/subscribe channel carrying input, system and app events |
| **Module** | A pluggable hardware add-on on the STARK module bus (IR/NFC/Sub-GHz) |
| **Screen** | A UI view pushed onto the screen stack; owns rendering for its region |
| **Surface** | A drawable RGB565 buffer (full screen on host, a band on target) |
| **Band** | A horizontal slice of the display rendered and flushed in one DMA transfer |
| **Scenario** | A Wokwi CI automation YAML that drives inputs and asserts serial output |
