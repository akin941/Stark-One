# DECISIONS — Architecture Decision Records

Format: Context → Decision → Alternatives rejected (with reasons) → Consequences →
Revisit trigger. Status is one of `accepted`, `provisional`, `superseded`.

Facts about tool versions were verified in **September 2026**; each ADR that depends on
a version states how to re-verify it.

---

## ADR-0001 — MCU: ESP32-S3

**Status:** accepted

**Context.** We need a handheld device with a colour SPI display, six keys, several
radios on shared buses, a file system, USB, and enough headroom that the UI never feels
slow. It must be simulatable, cheap, available, and supported for years.

**Decision.** ESP32-S3, as `ESP32-S3-DevKitC-1` for prototyping and
`ESP32-S3-WROOM-1-N16R8` on the custom PCB.

**Alternatives rejected**

| Option | Why rejected |
| --- | --- |
| STM32 (F4/G4/H7) | Better determinism and analogue, but no integrated radio, weaker single-vendor ecosystem for our mix, and Wokwi's STM32 support is far behind its ESP32 support — losing simulation-first development is too expensive |
| nRF52840 | Excellent BLE and power, but no Wi-Fi, less RAM, less display bandwidth, and BLE is not in our roadmap |
| ESP32 (original) | Fewer GPIO after flash/PSRAM, no native USB, older silicon, no benefit |
| ESP32-C6 (RISC-V) | Single core, less RAM; the UI + radio worker split wants two cores |
| RP2040 / RP2350 | Great PIO and cost, but no radio, weaker vendor RTOS story, and we would still need a second chip for Wi-Fi/USB stack maturity |
| Raspberry Pi Zero-class Linux | Boots in seconds, draws too much, overkill for a pocket tool, and destroys the real-time story |

**Consequences.** Octal PSRAM on the N16R8 consumes GPIO 26–37 — the pin map is planned
around that from day one ([HARDWARE.md §1](HARDWARE.md)). We inherit ESP-IDF as the
framework question (ADR-0002).

**Revisit trigger:** if the module bus grows beyond what one SPI + one I²C can serve, or
if power targets fail badly at V2.

---

## ADR-0002 — Framework: ESP-IDF (not Arduino, not PlatformIO, not Zephyr, not Rust)

**Status:** accepted

**Context.** The project is long-lived, modular, and destined for a custom PCB. We need
component-level modularity, Kconfig, a real build system, first-class driver access
(`esp_lcd`, RMT v2, SPI master with DMA), and a supported upgrade path.

**Decision.** ESP-IDF, using its native CMake component model.

**Alternatives rejected**

| Option | Why rejected |
| --- | --- |
| Arduino-ESP32 | Fast to start, but the abstraction hides exactly the peripherals we need to control (RMT carrier config, SPI bus sharing, DMA band transfers), its component model is ad-hoc, and it lags ESP-IDF releases. It is a prototyping layer, not a product platform |
| PlatformIO (`platform-espressif32`) | Pleasant DX, but its ESP-IDF support is community-maintained and version-lagging — as of Sep 2026 the mainline platform tracks ESP-IDF 5.4-era releases while ESP-IDF itself is at v6.1. Being a full minor version behind blocks `idf.py wokwi` (needs ≥ 6.0) and delays security fixes. Forks (pioarduino) exist but add another maintenance dependency |
| Zephyr | Excellent architecture and portability, but a far heavier learning curve, weaker ESP32-S3 peripheral coverage than the vendor SDK, and no Wokwi integration story |
| ESP-HAL / Embassy (Rust) | Genuinely attractive for safety and ergonomics; rejected for V0 because ESP32-S3 driver coverage (esp_lcd-equivalent, RMT, SD) is thinner, the team is one developer plus agents whose C output is more reliable than their `no_std` Rust, and mixing Rust with vendor components would complicate the build. Revisit for a V2 app-layer experiment |
| NuttX / FreeRTOS bare | Loses the vendor's driver ecosystem for no gain |

**Consequences.** ESP-IDF idioms everywhere; `esp_err_t` is translated at component
boundaries (ARCHITECTURE §5) so the core stays portable. CMake + Kconfig are the build
and configuration languages.

**Revisit trigger:** if Espressif's Rust support reaches parity for display + RMT + SD,
reconsider for greenfield apps only, never mid-milestone.

---

## ADR-0003 — ESP-IDF version: pin to v6.1, lock dependencies

**Status:** accepted (version facts verified Sep 2026)

**Context.** Verified state of the world: ESP-IDF v6.0 was released in March 2026;
v6.1 is the current release line (latest minor), with v6.0.3 as the newest v6.0 patch;
v5.5 entered its maintenance period in July 2026; each release is supported 30 months
(12 months "service", 18 months "maintenance"), and Espressif recommends starting new
projects on an in-service release. Separately, `idf.py wokwi` — the officially supported
Wokwi integration — **requires ESP-IDF ≥ 6.0**.

**Decision.** Pin to **ESP-IDF v6.1** (exact tag). Record it in `.idf-version`, in
`scripts/setup.sh`, and in CI as the Docker image `espressif/idf:v6.1`. Commit
`dependencies.lock` so registry component versions are reproducible. Declare component
dependencies with caret ranges in `idf_component.yml` but let the lock file decide.

**Alternatives rejected**

| Option | Why rejected |
| --- | --- |
| ESP-IDF v5.5.x | Already in maintenance (bug/security fixes only) — a poor starting point for a multi-year project, and it cannot run `idf.py wokwi` |
| ESP-IDF v6.0.3 | Perfectly reasonable and better battle-tested; rejected only because v6.1 is the same architecture with a longer support runway. **This is the designated fallback** if a required component proves incompatible with v6.1 |
| Track `master` | No reproducibility, no support guarantee |
| No lock file | Registry components moving under us is the single easiest way to break a green build for reasons unrelated to our change |

**Consequences.** We inherit v6.0's breaking changes deliberately: legacy ADC/DAC/I2S/
timer/PCNT/MCPWM/RMT/temp-sensor drivers are gone (we use the new APIs from the start —
which is what IR on RMT v2 wants anyway), compiler warnings are errors by default
(aligns with our quality gate), MbedTLS moves to 4.x/PSA (irrelevant to V0), minimum
CMake is 3.22.1, and Kconfig syntax is esp-idf-kconfig v3.

**Revisit trigger:** STARK-0001 re-verifies the newest patch tag at bootstrap time; a
version bump is a deliberate task with a full CI run, never a drive-by change.

---

## ADR-0004 — Display stack: ILI9341 via `esp_lcd` + band rendering

**Status:** accepted

**Context.** 320×240 RGB565 is 153.6 kB — 30 % of the S3's internal SRAM for a single
full framebuffer, and we have decided against depending on PSRAM (ADR-0014).

**Decision.** Use `esp_lcd_panel_io_spi` with the registry component
`espressif/esp_lcd_ili9341`, **pinned to the exact version `2.1.0`** (`"==2.1.0"` in
`idf_component.yml`, not a caret range), with `dependencies.lock` committed. Render in
**bands** (default 320×40) into two DMA-capable buffers, ping-ponged, driven by a
region-based render API (ARCHITECTURE §6.5).

**Version pinning.** v2.1.0 was the newest published version as of September 2026 and
is the pinned target. **v2.0.2 is the documented fallback**: if 2.1.0 proves
incompatible with ESP-IDF v6.1 or misbehaves during bring-up, drop to `"==2.0.2"`,
record the reason in `docs/DEVELOPMENT.md`, and re-commit the lock file. Either way the
version is exact and the lock file is authoritative — a floating range on the one
component that owns our only display is not a risk worth taking. Version bumps are a
deliberate task with a full bring-up re-check, never a drive-by change.

**Alternatives rejected**

| Option | Why rejected |
| --- | --- |
| Full framebuffer in internal RAM | Costs 153.6 kB permanently, starving future radio buffers, for a UI that redraws small regions |
| Full framebuffer in PSRAM | Adds a hard PSRAM dependency, octal-PSRAM/Wokwi configuration risk, and slower DMA from external RAM |
| TFT_eSPI / Adafruit_GFX | Arduino-oriented; would drag in the Arduino layer or a fork of it, and they do not use the IDF DMA path well |
| Writing our own ILI9341 driver | Pointless duplication; the vendor component is maintained and small |
| Direct `spi_master` calls without `esp_lcd` | Loses queued DMA transactions and the panel abstraction that makes a future panel swap cheap |

**Consequences.** Screens draw in logical coordinates and never see bands; the surface
carries an origin offset. Full-screen redraws cost ~25 ms of SPI time at 40 MHz, so the
UI must invalidate regions, not screens — which is the discipline we want anyway.

**Revisit trigger:** if a future app genuinely needs random-access persistent pixels
(image viewer, spectrum waterfall), add an opt-in PSRAM framebuffer path behind the
same `stark_display_render()` interface.

---

## ADR-0005 — UI framework: in-house `stark_gfx` + `stark_ui`; LVGL deferred

**Status:** provisional (revisit at V0.7 / V2)

**Context.** LVGL 9.5 (Feb 2026) with `esp_lvgl_port` 2.9.x is the default answer for
ESP32 GUIs, and it is genuinely good: widgets, layouts, animations, theming, input
device abstraction, and a maintained ESP integration.

**Decision.** For V0–V0.6, build a small in-house UI: `stark_gfx` (primitives + 1bpp
fonts over an RGB565 surface) and `stark_ui` (screen stack + status bar + list menu).
Re-evaluate LVGL at V0.7, when the widget needs of the lab apps are concrete.

**Why, concretely**

* The V0 UI is a list menu and a status bar. LVGL's value starts where layout
  complexity starts; we do not have that complexity for several milestones.
* LVGL wants a framebuffer or its own partial-render buffers plus a port layer, and it
  brings its own threading/lock model. Combined with band rendering (ADR-0004) that is
  two buffering strategies to reconcile at the exact moment we are also bringing up a
  panel for the first time.
* Host-testing pure C drawing and menu-model code takes an afternoon; host-testing LVGL
  screens takes a harness.
* Binary size and RAM: LVGL's baseline is affordable but not free, and V0's whole point
  is to establish a clean, measurable baseline.
* The cost of deferring is bounded: apps only ever see `stark_screen_t`, so an LVGL
  backend can later implement that same contract, app by app.

**Alternatives rejected (for now)**

| Option | Why rejected now |
| --- | --- |
| LVGL 9.5 + `esp_lvgl_port` from V0 | Adds a large dependency and a second buffering model during the riskiest bring-up; over-serves a list menu |
| LVGL 8.x | Older; no reason to start on the previous major |
| μGUI / GUIslice / embedded-gfx | Small like ours but unfamiliar, less maintained, and we would still write the menu layer |
| Immediate-mode GUI (imgui-style) | Redraws everything every frame — exactly what band rendering cannot afford |

**Consequences.** We own our text rendering, fonts and widgets. That is real work
(~1500 lines by V0.1) and real control. Anything LVGL-shaped that we start wanting
badly — animations, complex layouts, charts — is the signal to revisit.

**Revisit trigger (explicit):** when two or more of these are true — we want animated
transitions, a chart/waterfall widget, scrollable rich text, or on-screen keyboard —
raise an ADR to adopt LVGL behind `stark_screen_t`.

---

## ADR-0006 — Concurrency: one UI task, cooperative apps, one event bus

**Status:** accepted

**Decision.** Apps run cooperatively inside a single UI task via screen callbacks. All
cross-context communication goes through one event bus. Apps that need background work
spawn their own FreeRTOS task and talk back through the bus.

**Alternatives rejected**

| Option | Why rejected |
| --- | --- |
| One task per app | Stack memory per app, lifecycle complexity, and races around the shared display for no user-visible benefit |
| Direct callbacks between components | Produces the dependency graph we are explicitly avoiding |
| Multiple queues per subsystem | Harder to reason about ordering; one bus with a type mask is simpler and observable |
| A full app sandbox / dynamic loading | Enormous complexity for a single-user tool; would also invite untrusted third-party apps we do not want to support |

**Consequences.** A slow app callback stutters the UI — so the contract is "no callback
blocks > 16 ms", enforced by frame-overrun counters in `stark_diag`.

---

## ADR-0007 — Storage: FATFS on microSD for data, NVS for settings

**Status:** accepted (implemented at V0.2)

**Decision.** microSD over the shared SPI2 bus with FATFS for user-visible data
(captures, logs, exports); NVS in internal flash for settings and small state.

**Alternatives rejected**

| Option | Why rejected |
| --- | --- |
| SDMMC (4-bit) instead of SPI | Faster, but consumes dedicated pins we do not have after the octal-PSRAM reservation, and is unavailable in the Wokwi SD part |
| LittleFS on internal flash for user data | Robust, but not removable — users must be able to pull the card and read captures on a PC. Reconsider as a *fallback* store when no card is present |
| SPIFFS | Deprecated in practice; no directories, poor wear behaviour |
| Everything in NVS | Wrong tool for kilobyte-to-megabyte captures |
| FAT on internal flash partition | Possible as a no-card fallback; deferred |

**Consequences.** FAT means no power-loss atomicity — writes use temp-file-plus-rename
and a documented "safe eject". Card removal during write must degrade, not panic.

---

## ADR-0008 — Buses: one shared SPI2, one shared I²C, arbitration through owners

**Status:** accepted

**Decision.** `SPI2_HOST` is shared by display, microSD and Sub-GHz with per-device CS
and per-device clock speeds; `I2C_NUM_0` is shared by NFC, module ID EEPROMs and lab
I²C use. `stark_board` initialises the buses; drivers attach as devices; access is
serialised through the owning component (ARCHITECTURE §8).

**Alternatives rejected**

| Option | Why rejected |
| --- | --- |
| A dedicated SPI host per peripheral | The S3 has only two general-purpose hosts; spending both here blocks future expansion |
| Bit-banged buses for the slow peripherals | Wastes CPU and complicates timing for no benefit |
| A global bus mutex | Simpler but hides ownership; the display's DMA transfers need to be the scheduling point, not a lock anyone can take |

**Consequences.** Concurrent SD and display access is the known hot spot; V0.2's exit
criteria test it explicitly (large file write while rendering).

---

## ADR-0009 — Module architecture: explicit static app registry (V0), hardware module discovery deferred

**Status:** accepted

**Context.** Two separable questions get conflated under "modularity": how *software*
apps are discovered, and how *hardware* modules are discovered. They belong to
different milestones and deserve different answers.

**Decision — software apps (V0).** Apps are listed in a single explicit, static
registry: `components/stark_app/app_registry.c` holds a `const stark_app_t *const
stark_apps[]` array with `extern` declarations from an `app_list.h`. Adding an app is
one new directory plus one line in that array. No linker magic, no sections, no
iteration over `__start_/__stop_` symbols.

**Decision — hardware modules.** Deferred **in full** to V0.5. No capability bits, no
`caps_required` field, no EEPROM descriptor, no capability-gated menu items exist in
V0. The module bus milestone introduces them together, informed by two real drivers
(IR and NFC) that will already exist by then.

**Alternatives rejected**

| Option | Why rejected |
| --- | --- |
| **Link-time section registration (previously chosen for V0)** | Rejected for V0: it is unusual to debug, depends on linker-fragment behaviour that varies with build configuration, and can silently drop apps when sections are garbage-collected. It buys only the avoidance of one line in one array — a cost that does not exist yet at four apps. **Kept as a documented future option** (see revisit trigger) |
| Dynamically loaded app binaries | Needs a loader, an ABI, versioning and a security model. Not earned |
| Capability gating shipped early "so the menu need not change later" | This was the previous rationale and it was wrong: it is a speculative system with no V0 consumer. V0.5 changes the menu once, deliberately |
| Hard-coded module presence per board variant | Defeats the point of a module bus — but that is a V0.5 argument, not a V0 one |
| GPIO strapping for module ID | Only a handful of IDs, no revision or metadata — revisit at V0.5 against the EEPROM option |

**Consequences.** The array is a shared file, so two apps added in parallel touch the
same line region — a trivial merge conflict, accepted knowingly. Boot logs the registry
contents so the app list is always observable. `stark_app_t` in V0 carries no
`caps_required` field; V0.5 adds it along with the machinery that reads it.

**Revisit trigger:** when the app count passes ~15, or when apps start living outside
this repository, re-evaluate link-time registration — the mechanism is understood and
the migration is mechanical (replace the array with a section walk; the
`stark_app_find/list/launch` API does not change).

---

## ADR-0010 — Simulation strategy: identical artifact, zero simulator conditionals

**Status:** accepted

**Decision.** Wokwi loads `build/flasher_args.json` produced by the same `idf.py build`
we flash. No `#ifdef WOKWI`, no simulator-only sources, no simulator-only Kconfig
defaults. Where the simulator does not model a signal, firmware drives it anyway.

**Alternatives rejected**

| Option | Why rejected |
| --- | --- |
| A `SIM` build flavour with stub drivers | Creates two firmwares; the tested one stops being the shipped one — the classic simulator trap |
| Mocking the display in simulation | Defeats the purpose of simulating the display |
| Not simulating at all | We would lose the fastest feedback loop we have, especially while there is no hardware on the desk |

**Consequences.** Some behaviours simply cannot be verified in simulation (IR, NFC,
Sub-GHz — WOKWI.md §5). Those milestones split their acceptance criteria into
simulated and hardware-verified halves rather than faking coverage.

---

## ADR-0011 — Testing strategy: host tests for cores, Wokwi scenarios for integration

**Status:** accepted

**Decision.** Three layers: (1) host unit tests on pure cores with a standalone CMake +
Unity project, (2) Wokwi CI scenarios asserting on serial output and pin state,
(3) a written hardware bring-up checklist. Details in [TESTING.md](TESTING.md).

**Simulation asserts function, hardware asserts performance.** Wokwi acceptance
criteria are **functional and deterministic only**: does the sequence happen, in the
right order, with the right values, without drops or leaks. Wall-clock quantities —
boot time in milliseconds, frames per second, refresh duration, SPI throughput — are
**not** gates in simulation, because the simulator's time base is not the device's:
it does not model SPI wire time, flash latency, cache behaviour or CPU pipeline
effects, and its speed varies with the host running it. Such numbers may be *logged
and recorded* from simulation as informational trend data; they become pass/fail gates
only at the **V1 prototype** milestone, measured on hardware and written into
`docs/measurements.md`. Memory quantities (free heap, leak deltas, event-drop counters)
remain valid simulation gates — they are deterministic and architecture-driven, not
timing-driven.

**Alternatives rejected**

| Option | Why rejected |
| --- | --- |
| ESP-IDF on-target Unity tests only | Slow, needs hardware or a simulator per run, and cannot easily inject time |
| `idf.py --preview set-target linux` for host tests | Attractive (real IDF APIs on host) but Linux-only — the maintainer is on macOS, so it would force Docker for the fastest, most frequently run loop. Keep as an option for components that must exercise IDF APIs |
| No host tests, simulator only | Simulator minutes are quota-limited and the loop is ~10× slower; logic bugs deserve millisecond feedback |
| Hardware-in-the-loop CI | No hardware yet; revisit after V1 |

---

## ADR-0012 — Language: C11

**Status:** accepted

**Decision.** C11 for all firmware. C++ only if a dependency requires it, confined to
that component.

**Alternatives rejected.** C++17 (better type safety and RAII, but heavier binaries,
exception/RTTI configuration, mixed idioms with a C vendor SDK, and less predictable
output from coding agents); MicroPython (interpreter overhead, poor fit for driver
work); Rust (see ADR-0002).

---

## ADR-0013 — Use policy is an architectural constraint

**Status:** accepted

**Decision.** [docs/SECURITY_SCOPE.md](docs/SECURITY_SCOPE.md) is binding on feature
design. Capabilities that exist to defeat access control, recover keys from credentials
we do not own, clone identities, or jam are out of scope and are not implemented, not
stubbed, and not left as TODOs. Legitimate engineering capabilities — protocol
analysis, receive-first RF, IR/NFC work on our own hardware, bus debugging — are
designed properly and thoroughly.

**Consequences.** Transmit paths default to off, require explicit confirmation, are
rate/duty-limited, and log what they emit. Feature requests that cross the line are
closed with a pointer to the policy, not silently deprioritised.

---

## ADR-0014 — No PSRAM dependency before V1

**Status:** accepted

**Context.** The N16R8 module has 8 MB octal PSRAM, but enabling it changes the boot
path, the DMA rules and the simulator configuration, and it tempts the design toward a
full framebuffer (ADR-0004).

**Decision.** V0–V0.7 build with PSRAM disabled. Band buffers and all state live in
internal SRAM. `CONFIG_SPIRAM` stays off in `sdkconfig.defaults`.

**The simulator nevertheless models the real SKU.** `diagram.json` declares
`flashSize: 16`, `psramSize: 8` and octal PSRAM so the simulated part matches the
ESP32-S3-WROOM-1-**N16R8** we will actually build on. Firmware simply does not use the
PSRAM. This is deliberate: it continuously proves that V0 runs on the production SKU
*without* depending on PSRAM, rather than proving it runs on a convenient
smaller-memory fiction. A future `CONFIG_SPIRAM=y` experiment then changes one config
symbol, not the simulated hardware.

**Alternatives rejected.** Enabling PSRAM "just in case" (unused complexity, and it
quietly hides memory regressions); requiring PSRAM in the architecture (locks us to
R8 modules and complicates the Wokwi configuration).

**Revisit trigger:** a feature with a genuine multi-hundred-kilobyte buffer need —
waterfall display, long RF capture, image viewer — at which point PSRAM is enabled with
a measured before/after.

---

## ADR-0015 — Console on UART0 for V0

**Status:** accepted

**Decision.** Keep the ESP-IDF console on UART0 (GPIO 43/44) in `sdkconfig.defaults`
rather than USB Serial/JTAG, for V0.

**Rationale.** It is the path both the Wokwi serial monitor and the DevKitC-1's on-board
bridge show without configuration, and CI scenarios assert on serial output. USB CDC is
simulated on the S3 and remains available later, but switching the console is a change
with no V0 benefit and a CI-breaking failure mode.

**Revisit trigger:** when USB device support becomes a feature (V2), move the console
deliberately and update every scenario in the same change.
