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

**Addendum (2026-11-24, STARK-0708): the V0.7 re-evaluation, scored.** ADR-0023 §11 set the
trigger for V0.7 and STARK-0708 scores it against the apps actually built (eighteen:
About, Diagnostics, Self-Test, App Test, Buzzer Test, Display Test, Input Test, Hello, GPIO,
I2C, IR, NFC, SPI, Sub-GHz, UART, Modules, Settings, Storage). The four triggers above, each against the code and the scenarios:

| Trigger | Built? | Evidence | Scored |
| --- | --- | --- | --- |
| Animated transitions | no | No app, dialog or screen change animates anything; `stark_ui` pushes and pops screens and repaints bands. Nothing in V0.1 to V0.7 asked for motion; the live views (RSSI meter, the Measure row, the UART monitor) redraw values, they do not move objects | **false** |
| A chart or waterfall widget | no | The Sub-GHz meter is a number and a 12-cell text bar and the scan a peak line; the Measure row is a text result; no V0.7 app plots a series. A spectrum or waterfall would be new work that no V0.x task owns | **false** |
| Scrollable rich text | partly | The UART monitor is scrollable text: a 4 kB ring shown as hex or ASCII rows with UP, DOWN and page keys (`uartview_window` and `uartview_scroll`, pure code with a host test). It is plain fixed-width rows of one font, not styled, wrapped or mixed text, and the in-house menu already scrolls a long list with a bar. Counted as met, as ADR-0023 §11 counted it, because the monitor is the one place the in-house widgets needed a window onto a buffer | **true (conservatively)** |
| An on-screen keyboard | no | Every input in V0.7 is a fixed list or a nibble picker: the I²C register and bytes, the SPI bytes, the UART send, the pin modes and rates. No app takes free text; file names are generated (`uart_0001.log`, `report_0001.txt`, `rf_0001.rf`) | **false** |

**Outcome: one trigger of four, so fewer than two: keep the in-house UI.** ADR-0005 stays
provisional (revisit at V2) and no ADR to adopt LVGL is raised. The cost side was measured:
V0.7 added 88.8 kB of image for five apps and the lab and self-test services without a UI framework
(docs/measurements.md "V0.7 exit criteria"); the host-testable pure views (`gpio_views`,
`i2c_views`, `spi_views`, `uart_views`, `selftest_views`) and the pinned frames kept the apps
cheap to test, which is the property ADR-0005 bought. What would change the answer: **a second
trigger** — a Sub-GHz spectrum or waterfall view (the scan's data already exists), animated
transitions asked for by the V1 hardware's larger or touch panel, or an app that needs free text
(an SSID or file name, in a V2 radio milestone). Any one of those together with the monitor's
scrolling makes two, and then the next milestone raises the ADR behind `stark_screen_t`
instead of deciding in a task.

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
criteria test it explicitly (large file write while rendering). Under ADR-0018 V0.2
verifies the software half (the UI keeps rendering while a worker writes through the
same storage code path, emulator); the physical half — real bus contention, band-flush
stalls during SD commands, signal integrity — stays **open** until the V1 prototype.

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

**Location amended by ADR-0016:** from STARK-0104 the array lives in
`main/app_registry.c` (the composition root), so adding an app edits nothing under
`components/`.

**Revisited at STARK-0506 (V0.5, 2026-11-05): keep the static array.** The trigger is
about 15 apps, or apps from outside the repository. V0.5 has 12 apps, all in this
repository. Adding Modules (STARK-0505) took one new directory and one line each in
`app_registry.c` and `app_registry.h`, and no merge conflict arose. `caps_required`
fits the array: gating needs a field, not a registration mechanism. Link-time sections
would still bring the debugging and garbage-collection costs listed above and would
save two lines. Look again at the same trigger; V0.6 and V0.7 together are expected to
reach it.

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

---

## ADR-0016 — The app registry array lives at the composition root

**Status:** accepted, implemented (STARK-0104). Amends the
file location in ADR-0009; everything else ADR-0009 decided stands.

**Context.** ROADMAP V0.1 exit criterion 1 requires that a new app be added "in a single
new component directory … with no edits to `components/`". ADR-0009 put the explicit
static array in `components/stark_app/app_registry.c`, so every new app edits
`components/` by construction. The two cannot both hold. What ADR-0009 actually decided
— an explicit, static, observable list with no linker sections — does not depend on
which directory the array lives in.

**Decision.** The `stark_apps[]` array and its `extern` declarations move to
`main/app_registry.{c,h}`. `stark_app_init()` receives the array and its length from
`app_main()` (dependency injection from the composition root, which already owns every
other wiring decision). Adding an app becomes: a new `apps/app_<name>/` directory plus
one declaration and one array line in `main/app_registry.{c,h}`.

**Alternatives rejected**

| Option | Why rejected |
| --- | --- |
| Link-time section registration now | Exactly what ADR-0009 rejected at this app count; its revisit trigger (~15 apps, or apps outside the repository) has not fired — STARK-0506 re-evaluates it |
| Read exit criterion 1 loosely ("one registry line in `components/` is fine") | Leaves the stated criterion unmet and the core component edited by every app author |
| A registry component under `apps/` | Works, but adds a component whose only job is a list; `main` is already the place that knows which pieces make up this firmware |

**Consequences.** `components/` no longer names any app; `check_layers.py` keeps
forbidding every component but `main` from requiring an `app_*`. The registry is still
one shared file (the accepted merge-conflict cost of ADR-0009), still logged at boot.
The `stark_app_find/list/launch` API is unchanged; only `stark_app_init()` gains
parameters.

**Revisit trigger:** the same as ADR-0009's.

---

## ADR-0017 — Free validation only: Espressif emulator mandatory, Wokwi optional

**Status:** accepted (owner decision, 2026-09-27). Amends ADR-0010 and ADR-0011 where
they name Wokwi as the integration tier; their substance — one artifact, no simulator
conditionals, performance gated only on hardware — is unchanged.

**Context.** Wokwi's Free plan has a monthly CI-minute quota. V0 used it up, and with
Wokwi as a required check every firmware change stopped until the quota reset. The
owner's decision is final: **the project never depends on a paid Wokwi plan**, and a
quota must not be able to stop normal development.

**Decision.**
* Mandatory validation is free and runs locally and in CI: Tier 1 host tests, Tier 2
  the pinned ESP-IDF build, Tier 3 Espressif's **esp-emulator** running the production
  merged image (`scripts/test_emu.sh`, pinned version and SHA-256 in
  `scripts/esp_emu.lock`).
* External-peripheral behaviour the emulator cannot model is covered by host fakes and
  test ports (Tier 4A/B) and, from V1, by physical HIL (Tier 4C) — never by a paid cloud
  simulator.
* Wokwi is **optional** (Tier 5): manual dispatch or the `wokwi` PR label, only while
  free quota exists, never a required check. Its failures are shown, not hidden.
* `docs/VALIDATION.md` holds the tier model, the verified emulator capabilities and
  limits, and the per-assertion coverage matrix that justified removing Wokwi from the
  mandatory contract.

**Alternatives rejected**

| Option | Why rejected |
| --- | --- |
| Paid Wokwi plan | Excluded by the owner: a recurring cost the project must not depend on |
| Wait for the monthly reset | Makes delivery hostage to a quota |
| Keep Wokwi required but skip it when the quota is gone | A skip that looks like a pass is a false green |
| QEMU (Espressif fork) | Viable, but esp-emu is the maintained Espressif emulator with native macOS/Linux builds, dual-core S3, GP-SPI/GDMA/LEDC and a GDB stub; one emulator is enough |

**Consequences.** The emulator cannot drive external GPIO pads and models no SPI
device, so key presses are injected at the input core's sampling boundary via GDB and
the pad → key-bit step is host-tested; pixels are validated on the host at the display
boundary; panel glass, switch wiring, buzzer and LED output are explicitly deferred to
HIL. `scripts/wokwi_gate.py` stays useful for optional Wokwi runs.

**Revisit trigger:** an esp-emu release that adds external-pad stimulus or SPI device
models (widen Tier 3), or the V1 prototype (add HIL as a mandatory tier for hardware
milestones).

---

## ADR-0018 — V0.2 storage: software-verified in simulation, physical microSD at V1; the storage-medium seam

**Status:** accepted (owner decision "Option A", 2026-10-23). Applies ADR-0017 to V0.2;
narrows ADR-0010's "one artifact" rule for exactly one component (below); refines
ADR-0008's arbitration wording. Does not close ADR-0008's shared-bus risk.

**Context.** ROADMAP V0.2 exit criteria 2 and 3 (a 100 kB write/read-back while the UI
renders; card removal mid-write) and ADR-0008's display/SD bus sharing need a microSD
card. esp-emu 0.44.0 models no SD card and no external SPI device (docs/VALIDATION.md
§2); paid Wokwi is excluded (ADR-0017). Requiring a physical card would move hardware
ahead of the V1 prototype.

**Decision.**
1. **Two levels of completion.** *V0.2 complete* means the storage and settings
   **software logic is verified** with free, local tiers: host tests with deterministic
   fakes and fault injection, the production image in esp-emu (NVS persistence across a
   reset, the no-card path), and an emulator storage test image (item 3). *Physical
   microSD behaviour is not claimed.* Each affected exit criterion is split into a
   V0.2 software half and a V1 physical-HIL half (ROADMAP V0.2, TASKS.md V0.2). These
   remain open until the V1 prototype: real microSD electrical behaviour, real
   insertion/removal, real card-detect behaviour, display/SD contention and timing on
   the shared SPI2 bus, simultaneous panel + SD traffic, signal integrity and clock
   limits.
2. **The storage-medium seam.** Production code targets the real ESP-IDF APIs:
   `esp_vfs_fat_sdspi_mount()` over `SPI2_HOST`, `sdmmc_get_status()` for presence, the
   POSIX/VFS file API over FATFS. The medium — "make a FAT volume appear at `/sd`, take
   it away, and say whether the card still answers" — sits behind one small interface,
   `stark_storage_medium.h`, provided by the L1 component `stark_storage_medium`
   (production source: `medium_sdspi.c`). Everything above it — mount state machine,
   presence polling, eject, paths, file helpers, the log writer — is identical in every
   build. This is the same link-time port pattern as `stark_hal_host.c` (ARCHITECTURE
   §6.2): the seam exists because the medium is genuinely hardware, not for the
   emulator's convenience, and production code contains no test medium, no fake path
   and no conditional.
3. **Substitutes live only in test trees.** Host tests link
   `test/host/support/fake_medium.c` (and a fault-injecting wrapper around the real
   POSIX I/O ops). The emulator storage test image `test/emu/storage_image/` is an
   ESP-IDF project that builds the repository's `main`, `apps/*` and every
   `components/*` **except** `stark_storage_medium`, which it replaces with
   `test/emu/storage_image/components/stark_storage_medium/` — a wear-levelled FAT
   volume on an internal-flash partition, mounted at `/sd` with
   `esp_vfs_fat_spiflash_mount_rw_wl()`, whose presence is a test-image variable that
   the emulator harness flips over GDB to simulate removal and reinsertion. Its own
   partition table adds that FAT partition. `scripts/check_emu_image.py` (CI `lint`)
   proves the two builds' component lists differ in exactly that component. The
   production merged image stays the artifact every other scenario runs (ADR-0010
   unchanged for it).
4. **Bus arbitration** (refines ADR-0008). The display and the SD card are two devices
   on `SPI2_HOST`; the arbiter is ESP-IDF's `spi_master` bus lock — every transaction is
   atomic per device, and `sdspi_host` holds the bus across a multi-transaction SD
   command. STARK adds no global bus mutex (ADR-0008 rejects one). The ownership rules
   are: only `stark_display` (UI task) issues panel transactions; only
   `stark_storage_medium`, called from inside `stark_storage` under its own mutex (the
   storage task or an app worker), issues SD transactions; no other code calls
   `spi_device_*` on SPI2. Whether that is fast enough — how long an SD command can
   stall a band flush — is measured on hardware at V1 and is the open half of ADR-0008.
5. **No card-detect pin in V0.2.** The pin map is unchanged (`SD_CS` = GPIO 8 only).
   Presence is polled: while mounted, `sdmmc_get_status()` (CMD13) once per
   `CONFIG_STARK_STORAGE_PROBE_MS`; while absent, a mount attempt every
   `CONFIG_STARK_STORAGE_RETRY_MS`; an I/O error triggers an immediate probe. A
   card-detect switch is a PCB decision (V1 hardware) and would only shorten detection.
6. **No automatic formatting.** A card that mounts but is not FAT is reported as
   unusable; firmware never formats or repairs user media in V0.2.
7. **Board capability.** `stark_board_pins()->sd_cs != -1` is the SD capability; no
   separate capability structure is added (nothing else would read it; ADR-0009 keeps
   capability bits for V0.5).
8. **Wokwi.** `sim/diagram.storage.json` is not created in V0.2: an artifact that no
   mandatory check runs would rot. The emulator storage test image replaces it.

**Alternatives rejected**

| Option | Why rejected |
| --- | --- |
| B: physical microSD as a V0.2 prerequisite | Owner chose A; moves hardware ahead of V1 |
| Paid Wokwi with its microSD part | Excluded (ADR-0017) |
| `#ifdef`/Kconfig switch selecting a flash "SD" in the production image | A simulator conditional in shipped firmware (ADR-0010) and a fake storage path in production |
| Making the production image fall back to internal-flash FAT when no card is present | Changes product behaviour to suit tests; ADR-0007 defers a no-card fallback store |
| Abstracting all file I/O behind a STARK VFS so tests need no FAT | Reshapes production around the emulator; the POSIX API over FATFS is the real contract |
| A STARK bus mutex around every SPI2 user | Rejected in ADR-0008; duplicates the driver's bus lock and hides ownership |

**Consequences.** V0.2's report lists software-, emulator- and host-verified behaviour
separately from deferred physical evidence. The V1 prototype inherits concrete SD gates
(ROADMAP V1, TESTING §5). One extra firmware build runs in CI (`esp-emulator-storage`).
The ADR-0008 risk stays **open**: the emulator proves the UI task keeps rendering while
a worker writes through the same code path, not that the real bus shares well.

**Revisit trigger:** an esp-emu release with an SD-over-SPI model (move exit 2/3
evidence onto the production image and retire the storage test image), or the V1
prototype (run the deferred physical gates).

---

## ADR-0019 — V0.3 infrared: protocol-verified in simulation, optical IR at V1; RMT loopback as evidence

**Status:** accepted (owner decision, 2026-10-24). Applies ADR-0017 to V0.3 the way
ADR-0018 applied it to V0.2.

**Context.** ROADMAP V0.3 needs a receiver, an IR LED and our own remotes for its
physical claims, and SECURITY_SCOPE §4.7 asks for test vectors from our own hardware.
None exists before the V1 prototype. Paid Wokwi is excluded, and Wokwi's RMT is
transmit-only (WOKWI.md §5). esp-emu 0.44.0 offers `--rmt-loopback TX:RX`, which feeds a
TX channel's symbols into an RX channel.

**Decision.**
1. **Two levels of completion.** *V0.3 complete* means **software / protocol
   verified**: codecs, the raw format, the RMT RX/TX integration, transmit gating, the
   capture → save → browse → replay data flow, the app, storage and diagnostics. It
   never means physical IR verified. Each exit criterion is split into a V0.3 software
   half and a V1 physical half (ROADMAP V0.3). Open until V1: IR LED drive and its
   transistor driver, carrier frequency and duty on a logic analyser, the receiver
   module's demodulation and electrical behaviour, reception from real remotes, a real
   device responding, range, angle, ambient-light and noise sensitivity, LED output
   power, timing accuracy on silicon, GPIO/RMT signal integrity.
2. **What the RMT loopback proves — and only that.** Verified with ESP-IDF's
   `ir_nec_transceiver` example on the esp32s3 model: `--rmt-loopback 0:2` (the first TX
   channel into the first RX channel; the TRM's absolute index `0:4` delivers nothing)
   makes the RX channel receive the transmitted symbols, decoded back to the sent
   address and command. The looped waveform is the **pre-carrier baseband** with exact
   durations: no carrier, no jitter, no optics, no receiver. A loopback pass therefore
   proves the production firmware's RMT TX path, encoder, RX path, symbol conversion,
   decoder, events, app and storage work together — never that light left an LED or
   that a receiver could demodulate it. Timing tolerance is proven on the host with
   jittered vectors, not by the loopback.
3. **Test vectors.** SECURITY_SCOPE §4.7's intent is that vectors never come from other
   people's devices. Before hardware, every vector is **spec-derived**: generated
   deterministically from the published protocol timings (with seeded jitter and
   tolerance-edge cases) by `scripts/gen_ir_vectors.py`, committed, and labelled
   "spec-derived, not recorded". Vectors **recorded from our own remotes** join at V1;
   until then ROADMAP V0.3 exit 1's "recorded" half is open.
4. **Transmit guardrails apply to IR.** SECURITY_SCOPE §4 is written for radio; IR
   follows it: receive-first (transmit is a separate, confirmed action naming protocol,
   carrier, duration and payload), no unattended loops (at most 3 repeats per
   confirmation, at least 1 s between transmissions, at most 1 s of signal per
   transmission — a pure, host-tested gate in `stark_ir`), every transmission logged.
   No brute lists, no code databases, no "universal remote" sweeps.
5. **Loopback and seeding live in the harness and the test tree.** The scenario key
   `rmt_loopback` makes the runner pass the flag; captures used for replay scenarios are
   seeded into the storage test image's FAT partition at build time
   (`test/emu/storage_image/seed/`). Production code has no loopback mode, no
   simulator branch and no test signal.

**Alternatives rejected**

| Option | Why rejected |
| --- | --- |
| A physical breadboard as a V0.3 prerequisite | Owner decision; hardware is V1 |
| A firmware loopback/self-receive mode for tests | A test path in production firmware (ADR-0010) |
| Vectors taken from public capture collections | Other people's devices (SECURITY_SCOPE §4.7) and licensing; spec-derived vectors are exact and reproducible |
| Claiming loopback as transmit evidence | It carries no carrier and no light (item 2) |

**Consequences.** V0.3's report separates software, emulator/loopback, host and
deferred physical evidence. ROADMAP V1 exit 9 and the V1 hardware table collect the IR
gates. `docs/bringup/v1-ir.md` (STARK-0307) is written now and filled in at V1.

**Revisit trigger:** an esp-emu release modelling carrier modulation or an IR
receiver; the V1 prototype.

---

## ADR-0020 — V0.4 NFC: protocol-verified against a test-only fake PN532, physical NFC/RF at V1

**Status:** accepted (owner decision, 2026-10-25). Applies ADR-0017 to V0.4 the way
ADR-0019 applied it to V0.3.

**Context.** ROADMAP V0.4 reads, identifies and writes NDEF to our own 13.56 MHz tags
through a PN532-class module on the shared I²C bus. No module, antenna or tag exists
before the V1 prototype. Wokwi has no NFC part, and paid Wokwi is excluded. esp-emu
0.44.0 has no external I²C device model (its only device side channels are UART1 and the
RMT loopback). Verified with ESP-IDF's `i2c_basic` example on the esp32s3 model: the I²C
master bus initialises, and a transaction to an absent address returns
`ESP_ERR_INVALID_RESPONSE` at once, ESP-IDF's result for an address NACK. The emulator
can therefore run the production firmware against an **empty** bus, never against a
PN532.

**Decision.**
1. **Two levels of completion.** *V0.4 complete* means **NFC software / protocol
   verified**: NDEF encode/decode, PN532 framing and protocol handling, tag detection
   and identification, the Type 2 read/write data flow, the app, the dump format and
   storage, error, timeout and recovery behaviour. It never means a real tag was read.
   Each ROADMAP V0.4 exit criterion is split into a software half and a V1 physical
   half. Open until V1: real PN532 I²C electrical communication and initialisation on
   the board, the antenna and RF field, detection range and orientation, real ISO 14443
   timing, real UID/tag reads, real NDEF reads and writes, removal timing, noisy or weak
   tags, several tags in the field, power and current, and bus interaction with the rest
   of the device.
2. **The production transport is the real one.** `stark_nfc_transport` (L1) talks to
   the PN532 at its I²C address 0x24 through ESP-IDF's `i2c_master` driver on the
   board's `I2C_SDA`/`I2C_SCL`. The PN532 protocol driver above it is a pure L2 core
   that receives its transport by injection (as the storage engine receives its I/O
   table, ARCHITECTURE §6.9b). Production code has no fake mode, no simulator branch
   and no `#ifdef` of any kind for testing.
3. **The fake PN532 is test-only and models the protocol.** `test/fake/fake_pn532.c`
   is a pure C model of the chip as seen over I²C: the status byte and ready bit, ACK
   and NACK frames, information frames with length and data checksums, the commands the
   driver uses, an application-error frame, and the memory of a simulated tag (Type 2
   pages, GET_VERSION, writes, ATQA/SAK/UID for identification-only types). Fault
   switches inject NACK, checksum errors, truncation, timeouts, bus errors, unexpected
   response codes, and a tag that appears, disappears or vanishes mid-transaction. It
   answers frames, never finished tag objects, so every test runs production API →
   production driver → transport seam → fake device. It is linked by the host tests and
   by the emulator test image's substitute `stark_nfc_transport`
   (`test/emu/storage_image/components/`), exactly as ADR-0018 §3 substitutes
   `stark_storage_medium`; `check_emu_image.py` allows both substitutions and nothing
   else. RF and analogue behaviour are not modelled.
4. **Vectors are spec-derived.** PN532 frames follow the NXP PN532 User Manual
   (UM0701-02), Type 2 tag memory follows the NFC Forum Type 2 Tag specification and
   NXP's NTAG213/215/216 data sheet, and NDEF follows the NFC Forum NDEF and RTD
   (URI, Text) specifications. They are generated deterministically by
   `scripts/gen_nfc_vectors.py`, committed, and labelled "spec-derived, not read from a
   tag". This is the reading of SECURITY_SCOPE §4.7 that ADR-0019 §3 made for IR:
   nothing comes from other people's tags, and vectors read from our own tags join at V1.
5. **Security boundary (SECURITY_SCOPE §2, §3).** Reading and identifying our own
   tags, parsing standard NDEF, writing NDEF to our own writable NFC Forum Type 2 tags,
   and storing or displaying those captures. MIFARE Classic and ISO-DEP tags are
   identified from ATQA/SAK/UID only: no sector authentication, no keys, no key
   dictionaries. Writes touch only the NDEF area inside the capability container's
   declared size, never pages 0–3 (UID, lock bytes, capability container), dynamic lock
   bytes, OTP or configuration and password pages, so nothing can lock a tag
   permanently. Writing requires a confirmation naming the tag UID and the content.
   There is no dump restore, UID writing, card emulation (`TgInitAsTarget`), password
   authentication or brute forcing; none of these is stubbed. The RF field is on only
   while the NFC app polls.
6. **The emulator is integration evidence only.** On the production image it shows the
   no-module path on an empty bus. On the test image, over the fake, it shows the app,
   the UI, storage and the NFC task working together. It is never NFC or RF evidence.

**Alternatives rejected**

| Option | Why rejected |
| --- | --- |
| A physical breadboard as a V0.4 prerequisite | Owner decision; hardware is V1 |
| Mocking the driver's API (returning finished tag records) | Owner requirement: tests must exercise the production driver's framing, ACK, checksum, timeout and retry paths |
| A test or emulator mode inside `stark_nfc` | A test path in production firmware (ADR-0010) |
| Waiting for an esp-emu I²C device model | None exists in 0.44.0; the fake gives deterministic fault injection a device model would not |
| SPI fallback in V0.4 | ROADMAP names I²C first; SPI is a revisit item if the V1 board needs it |

**Consequences.** V0.4's report separates software, fake-device, host, emulator and
deferred physical evidence. ROADMAP V1 exit 10 and the V1 hardware table collect the NFC
gates, and `docs/bringup/v1-nfc.md` (STARK-0405) is written now and filled in at V1.

**Revisit trigger:** an esp-emu release with an I²C device model, the V1 prototype, or a
module other than the PN532 (ST25R-class) being chosen.

---

## ADR-0021 — V0.5 module bus: protocol-verified against a test-only fake 24C02, physical module bus at V1

**Status:** accepted (owner decision, 2026-11-05). Applies ADR-0017 to V0.5 the way
ADR-0019 and ADR-0020 applied it to V0.3 and V0.4. Closes the V0.5 half of ADR-0009.

**Context.** ROADMAP V0.5 makes hardware modules discoverable: an ID EEPROM per slot,
capability bits, `caps_required`, gated apps, IR and NFC retrofitted, an inspector. No
connector, EEPROM or second module exists before the V1 prototype. esp-emu 0.44.0 has no
configurable I²C device model; found at STARK-0502, it acknowledges 0x50 and reads zeros
there, so the production image shows an unknown EEPROM in slot 0 and nothing in slot 1.
Reading the authoritative hardware documents gives this topology:

* **IR is onboard.** `IR_TX`/`IR_RX` are fixed GPIOs (HARDWARE §2.2), not connector pins,
  and the IR front-end is on the V1 PCB (HARDWARE §8).
* **NFC is a detachable off-the-shelf reader on the shared I²C bus** (HARDWARE §3, §7;
  not on the §8 PCB list). A PN532 breakout has no ID EEPROM; only the chip's own answer
  at 0x24 identifies it.
* **The connector has no presence pin** (HARDWARE §6). An empty slot and a module without
  an EEPROM look the same on the bus.
* **The 25 usable GPIOs are all spent** (HARDWARE §1, §2). Slot 0 can use the pins
  reserved for Sub-GHz (`SUBGHZ_CS` 40, `GDO0` 41, `GDO2` 42) as `MOD_CS`, `MOD_IRQ0/1`.
  `MOD_EN` and a second slot's chip select and IRQ lines have no GPIO.

**Decision.**
1. **Two levels of completion.** *V0.5 complete* means **software / module-protocol
   verified**: descriptor format, discovery, hotplug state machine, capabilities, app
   gating, arbitration logic, the IR/NFC retrofit, the inspector, and error and recovery
   behaviour. It never means a real module was plugged in. ROADMAP V0.5 exit 3, two
   modules sharing SPI without corrupting the display, is physical: it **becomes a V1 HIL
   gate** (ROADMAP V1 exit 11) and is not removed. Its software half is the arbitration
   logic.
2. **A capability counts only when a driver in this firmware confirms it.** A descriptor
   *declares* capabilities; `stark_module_confirm()` *confirms* them; only confirmed,
   unconflicted capabilities are effective and reach `caps_required`. A descriptor is
   device metadata. It is not a credential, grants nothing, and is never executed:
   nothing is loaded from it, its strings are sanitised, and the firmware has no way to
   write an EEPROM.
3. **Retrofit by provider, not by a new topology.** Entries in the module table come from
   three providers: *onboard* (IR, confirmed by `stark_ir` when the board has its pins),
   *bus device* (the PN532, confirmed by `stark_nfc` from the chip's answer, with or
   without an EEPROM) and *slot* (an ID EEPROM). A slot declaring NFC does not turn NFC on;
   it is shown as declared until the PN532 answers. IR and NFC protocol code is unchanged.
4. **Exit 2 under this topology.** "Unknown" EEPROM content gives a generic entry with no
   capabilities. An absent EEPROM gives a slot entry reading "no ID" with no capabilities,
   because a module without an EEPROM cannot be told from an empty slot. Neither ever
   fails the boot.
5. **Pins.** Slot 0 = EEPROM 0x50, `MOD_CS` 40, `MOD_IRQ0` 41, `MOD_IRQ1` 42. Slot 1 =
   EEPROM 0x51, identification only: no chip select or IRQ lines on this board, so an SPI
   claim is refused. `MOD_EN` is unassigned (the prototype ties it high). The arbitration
   core takes its pin table by injection and is tested with two fully wired slots. Which
   pins a second wired slot and `MOD_EN` get is a V1 hardware decision, recorded as a risk
   (ARCHITECTURE §13), not guessed here.
6. **The fake 24C02 is test-only.** `test/fake/fake_24c02.c` models the chip as seen over
   I²C: address ACK/NACK per device, the word-address pointer with auto-increment and
   wrap, and fault switches. Every test runs production API → discovery → descriptor
   parser → transport seam → fake. It is linked by the host tests and by the test image's
   substitute `stark_module_transport`; `check_emu_image.py` allows exactly that third
   substitution. Production code has no fake mode and no simulator branch (ADR-0010).
7. **The emulator is integration evidence only.** The production image shows the empty
   bus. The test image shows hotplug, the launcher, the inspector and the IR/NFC apps
   working together. Neither is evidence about connectors, pull-ups, power or SPI signals.

**Alternatives rejected**

| Option | Why rejected |
| --- | --- |
| A physical breadboard as a V0.5 prerequisite | Owner decision; hardware is V1 |
| Capabilities effective from the descriptor alone | A corrupt or wrong EEPROM would enable an app with no hardware behind it (owner: descriptor is not authorization) |
| Requiring an EEPROM for NFC | HARDWARE names an off-the-shelf PN532 module, which has none |
| Moving IR to the connector | Contradicts HARDWARE §2.2 and §8 |
| Assigning `MOD_EN` and slot 1 lines from the V0.7 lab UART or strapping pins now | A hardware trade-off with no consumer before V0.6; decided with the V1 board |
| Reading descriptors only at boot | ROADMAP requires hotplug |
| GPIO strapping for the module ID (ADR-0009 alternative) | Only a few IDs, no revision or name, and no free pins |

**Consequences.** The launcher dims NFC on an empty bus instead of opening a "no module"
screen; the screen remains for a module lost while the app is open. The V0.5 report
separates software, fake-device, host, emulator and deferred physical evidence.
`docs/bringup/v1-modules.md` (STARK-0505) is written now and filled in at V1.

**Revisit trigger:** an esp-emu release with an I²C device model, the V1 board's pin
allocation, or a module that must expose more than one slot.

## ADR-0022 — V0.6 Sub-GHz: RF-protocol-verified against a test-only fake CC1101, physical RF at V1

**Status:** accepted (2026-11-23). The owner delegated the V0.6 product decisions to the
implementer and asked for autonomous progress; every decision below is recorded here
rather than taken silently. Applies ADR-0017 to V0.6 as ADR-0019, ADR-0020 and ADR-0021
applied it to V0.3–V0.5.

**Context.** ROADMAP V0.6 adds a CC1101-class 433 MHz module: RSSI and band scan, raw
OOK/2-FSK capture to SD, generic non-security decoders, and gated replay of our own
captures. Its four exit criteria name our own transmitter, our own device's timing and
our own receiver; three of them are physical. No CC1101, antenna or test transmitter
exists before the V1 prototype, no paid tools are allowed (ADR-0017), and esp-emu 0.44.0
has no CC1101 or radio model. Its RMT loopback joins only the first TX channel to the
first RX channel, and IR already owns that pair (docs/VALIDATION.md §2). The module
interface from V0.5 is in place: slot 0 wires `MOD_CS` 40, `MOD_IRQ0` 41 (`SUBGHZ_GDO0`)
and `MOD_IRQ1` 42 (`SUBGHZ_GDO2`), claimed through `stark_module_claim()`.

**Decision.**
1. **Two levels of completion.** *V0.6 complete* means **software / RF-protocol
   verified**: register profiles, chip detection and capability confirmation, RSSI and
   scan logic, the capture pipeline and raw format, the decoders, the transmit policy and
   its refusals, the app, storage, logging, and error and recovery behaviour. It never
   means a radio transmitted or received. Exits 1–3 split into a software half (V0.6) and
   a physical half that **becomes ROADMAP V1 exit 12**. Exit 4 (no transmission without a
   confirmation, duty cycle respected) is enforced by firmware logic and is verified in
   full in software; its RF measurement (power and on-air time on a spectrum analyser) is
   in V1 exit 12.
2. **Capability bit 5, `STARK_CAP_SUBGHZ`.** A slot module declares `SUBGHZ | MOD_SPI |
   MOD_IRQ`. Only `stark_subghz` confirms it, after claiming the slot and reading the
   chip's `PARTNUM` = 0x00 and `VERSION` = 0x14 or 0x04 over SPI. A descriptor declaring
   it grants nothing (ADR-0021 §2). `SUBGHZ` without both wiring bits is an unsupported
   combination, and `SUBGHZ` is exclusive like `NFC`. Format v1's layout does not change:
   bit 5 was "unknown, listed and ignored" in V0.5, so older descriptors stay valid.
3. **The transport seam is `stark_subghz_transport` (L1).** It carries SPI bytes to the
   chip and the GDO data line as timed symbols: `xfer`, `rx_start`/`rx_take`/`rx_stop`
   for GDO2, `tx` for GDO0. Production uses `stark_module_spi` on the claimed slot and RMT
   on the claimed IRQ lines (asynchronous serial mode). The test image substitutes it with
   a test-only fake CC1101, the fourth allowed substitution in `check_emu_image.py`.
   Production code has no fake mode and no simulator branch (ADR-0010).
4. **The fake CC1101 is test-only.** `test/fake/fake_cc1101.c` models the chip as seen over
   SPI: the status byte, configuration registers, burst access, `PATABLE`, strobes and the
   IDLE/RX/TX states with `MARCSTATE`, status registers, and an RSSI derived from an
   injected "air" (signal level per frequency). Its data line replays injected symbol
   scenes into GDO2 and records what GDO0 sends. An "own receiver" switch feeds the
   recorded transmission back into GDO2, so a replay can be captured and compared.
   Scenes come from `scripts/gen_subghz_vectors.py`, an independent implementation, and
   are documented as spec-derived, never as recordings of a real device.
5. **Band lock.** V0.6 tunes only 433.05–434.79 MHz, for receive, scan and transmit. The
   register profile refuses any other frequency. Other bands need antenna and matching
   work and wait for V1 hardware (HARDWARE §7).
6. **Transmit policy** (SECURITY_SCOPE §4, a pure host-tested core; none of it is
   user-adjustable or a Kconfig option):
   * **Power:** `PATABLE` is the constant 0x60, about 0 dBm conducted at 433 MHz, under
     the 10 mW ERP limit with margin for antenna gain. No code path writes another value.
   * **Duty cycle:** at most 360 s of transmission in any rolling 3600 s (10 %), and after
     each transmission an off-time of at least nine times its length.
   * **Length:** one transmission is at most 2048 symbols and at most 2 s.
   * **Source:** only a raw capture file in the device's own format. There is no
     transmission synthesised from decoded bits, no editor, and no import of
     third-party code sets.
   * **One per confirmation:** a modal dialog names the frequency, the duration and the
     file, and says "Own devices only". OK sends it once. There is no repeat, loop,
     sweep or schedule.
   * **Logged:** `subghz: tx <file> <freq> Hz <n> sym <ms> ms`, as well as every refusal
     with its reason, to the console and to the log file.
   The duty budget lives in RAM and starts empty at boot. That is acceptable because no
   transmission happens without a key press.
7. **Decoders are generic.** PWM (pulse-width), PPM (pulse-distance) and Manchester
   demodulators turn symbols into bits with their measured timing. There are no protocol
   names, no device database and no decoding of rolling or encrypted codes. Their output
   is display-only and cannot be transmitted.
8. **The emulator is integration evidence only.** The production image has slot 0
   "generic (bad magic)" (ADR-0021), so Sub-GHz stays dimmed. The test image shows
   detection, RSSI, scan, capture, save, decode, the transmit dialog and policy, and a
   replay captured by the fake's own receiver. None of it shows that a signal left an
   antenna. The production RMT and GDO path is exercised only on hardware; IR's RMT
   evidence (ADR-0019) covers the driver calls in general, not these pins.

**Alternatives rejected**

| Option | Why rejected |
| --- | --- |
| Buying a CC1101 module and a test transmitter now | Hardware is V1 (ADR-0017, owner policy) |
| A fake CC1101 inside production code with a test flag | ADR-0010: one code path |
| Using the esp-emu RMT loopback for the GDO lines | Only TX 0 → RX 0 loops, and IR owns that pair; a second pair delivers nothing |
| Full CC1101 band coverage (315/868/915 MHz) now | Needs antenna and matching; SRD 433 is the documented target (HARDWARE §7) |
| The legal maximum (+10 dBm) as the power cap | Conducted power plus antenna gain could exceed 10 mW ERP; a lab tool does not need range |
| Transmitting decoded or edited codes | Turns analysis into code synthesis (SECURITY_SCOPE §3, brute force) |
| A persistent duty budget in NVS | Flash wear for no gain: transmission is never unattended |

**Consequences.** A Sub-GHz app joins the Lab category with `caps_required =
STARK_CAP_SUBGHZ`. `stark_subghz` publishes `STARK_SYS_SUBGHZ` (id 7). Captures are
`captures/rf_NNNN.rf`. The V0.6 report separates software, fake-device, host, emulator
and deferred physical evidence. `docs/bringup/v1-subghz.md` (STARK-0606) is written now
and filled in at V1.

**Addendum (STARK-0603).** Four readings of §4 and the capture spec, recorded so they are not
taken silently: (1) the "2 s" of a capture is the **span of the kept signal** (the sum of the
symbol durations), not wall-clock time, so waiting for a remote costs nothing and a transmission
file can never exceed the limit of §6; (2) the `rf` parser **also refuses a frequency outside
433.05–434.79 MHz** — a file is our own capture, and the transmit policy of STARK-0605 still
checks the band itself; (3) the file is **streamed**, written through a cursor-driven formatter and
read by a byte-fed parser with a 160-byte line buffer, so neither side holds a 19 kB text;
(4) the 8 kB symbol buffer belongs to the app, which allocates it from the heap when a capture
starts or a file is opened and frees it when the app stops (the service borrows it while a
capture runs), instead of 8 kB of static RAM for the life of the firmware.

**Addendum (STARK-0604).** The decoder spec left these open; each is the conservative reading,
recorded so it is not taken silently (rules in docs/subghz-decode.md). (1) The **shortest
cluster** that sets the burst-splitting gap is the first histogram group with at least two
members, so one glitch shorter than everything else does not turn every low into a gap; "longer
than 4×" is strict. (2) A burst **drops the low after its last high** (the gap, or a low cut by
the end of the capture): pulse-distance needs a closing pulse and loses a last bit without one,
pulse-width does not check its last low. (3) **Unknown beats a guess**: two accepting kinds,
fewer than 8 or more than 128 bits (never truncated), a single cluster (all bits equal, a plain
clock) and clusters closer than 1.5× all decode to "unknown". (4) Manchester reads a burst that
starts on the second half of its first cell (a 0 whose low half merged into the silence) and one
that ends on a high whose low half is the gap; with a 2T symbol only one alignment can read. (5)
The conventions are the generic ones and the app shows the timing's reading, not a device's
meaning: long high = 1 (PWM), long low = 1 (PPM), high-low = 1 (Manchester); an inverted
convention reads as the complement. (6) The result is a fixed 232-byte struct of eight distinct
decodes with repeat counts and counters; the view lists four, the log all. (7) The vectors are
host-only (`test/fake/fake_subghz_decode_vectors.h`, separate from the scene catalogue the test
image carries), so the image's flash and heap do not change for them. The decoders add no
transmit path of any kind: nothing in the analysis can reach `tx()`.

**Addendum (STARK-0605).** The replay spec left these open; each is the conservative reading,
recorded so it is not taken silently. (1) **The duty window is RAM and does not persist, and a boot
may not reopen more than a fresh window.** §6 says it "starts empty at boot". It also starts with
the longest possible off-time already running (9 × 2 s = 18 s from the start of the service), as
if a maximal transmission had just ended. A reboot therefore cannot dodge the off-time rule, and
because the off-time alone caps the duty at 10 % (a 2 s send is followed by 18 s of silence, boot
included), power-cycling between transmissions cannot exceed the cap either; the 360 s budget and
the off-time agree at 10 %, so the window being RAM costs nothing. NVS stays rejected (flash wear,
and nothing transmits unattended). The ledger lives in the service, not the module: unplugging
and replugging the radio does not start a fresh window. (2) **Accounting.** A transmission is
charged its signal rounded up to a millisecond (at least 1); an attempt that reached the radio is
charged in full whether it succeeded, failed or timed out, because how long a carrier was up is
unknown after a failure; its end is the later of the planned end and the real end, and the off-time
runs from there. A refusal, at the request or on the transmit path, is not charged. A transmission
belongs to the window until 3600 s after it ended (the edge is exact: at 3 599 999 ms it counts, at
3 600 000 it does not), and the budget is exhausted exactly at 360 000 ms charged. (3) **The ledger
is 192 entries** (8 B each, 1.5 kB of static RAM; 180 × 2 s fill the budget) and is exact up to
192 transmissions an hour; with more (only short ones can) the two oldest merge, charged their sum
for as long as the later lives, so the window can only ever look fuller than it is. (4) **Only OOK
files are sent.** The one transmit profile is `OOK_TX`; a 2-FSK capture is refused as `file`. The
file's own rate is used, its frequency is rechecked (the parser already refuses out-of-band files),
and no field of a file can change the power, the length or the count. (5) **Four checks.** The
dialog's check, the request's, the service task's own on the transmit path, and a read-back from
the chip (the frequency registers and `PATABLE`) before `STX`; `cc1101_strobe()` still refuses
`STX`, so `cc1101_tx_start()` is the only way into TX. The transport also enforces 1–2048 symbols,
2 s and its own timeout, so a caller that skipped the policy still cannot exceed the limits. The
request carries the fingerprint of the symbols that were shown, and a request whose symbols hash
differently is refused as `file`. (6) **A confirmation buys one attempt.** The request is consumed
before the radio is touched; nothing retries a failure; `stop()`, any other request and a lost
module drop a request that has not started. (7) **The service mutex is held for the whole
transmission** (at most 2.1 s) rather than released around the transport call: the capture buffer
belongs to the app and is freed on exit, so letting `stop()` run while symbols are read from it
would be a use after free. The cost is that a key handled during a transmission would block the UI
task for up to 2.1 s, so the app swallows every key while one runs and the UI only waits when the
app is closed. (8) **The fail-safe is the line.** OOK sends no carrier while GDO0 is low; the RMT
channel ends low and is deleted after every transmission; if the chip cannot be shown to be IDLE
afterwards the module is dropped and reset when it answers again. The RMT path itself is exercised
only at V1, like the receive path. (9) **What the confirmation says**: the file, the frequency,
the duration, the symbol count, the duty budget left and "Own devices only". A refusal is an alert
with its reason (and, for the off-time, how long to wait) and is logged by the service.

**Addendum (STARK-0606).** The exit task left these open; each is the conservative reading,
recorded so it is not taken silently. (1) **The soak is two passes, not one long pass**, because
the first transmission cannot come before the 18 s boot lock has run out, so one pass of the whole
sequence already spans half a minute. Each pass runs detect, meter, a pulled and replugged module,
scan, capture, save, decode, one confirmed replay and one replay refused for the off-time. The
second pass's replay is 26 s after the first one's end, and the gates are those of the earlier soaks
(`drops=0`, first versus last `stark_diag` heap within 1 kB). (2) **The test image's fake gives
back its 8.4 kB recording after a transmission when nothing is listening** (`emu_sg_own_rx` 0:
`fake_cc1101_gdo0_release()`, called by the test transport only). Without it the allocation at the
first `STX` would have shown as an 8.4 kB "leak" in a gate that compares the first line, taken
before any module exists, with the last; with an own receiver the recording stays, because the
next capture plays it. Nothing in production changes. (3) **The stack peak is a fill-pattern read,
not an instrumented counter.** FreeRTOS fills a static task stack with 0xa5; after the soak a GDB
script counts the bytes still holding it from the low end, which is the untouched part (the method
is written in docs/measurements.md). The read covers the paths the soak exercises; a worst case
the soak does not reach is a V1 measurement (bring-up check 28). (4) **The V1 pass threshold for
conducted power is +3 dBm at `PATABLE` 0x60** (bring-up check 21), not the 0 dBm the ADR names as
"about": a real board's matching, a PA ripple of a few dB and the analyser's accuracy make "about
0 dBm" an interval, and the ERP limit (10 mW, +10 dBm) leaves 7 dB for the antenna. A board that
reads higher fails the check and the cap is lowered in a new task; it is never raised.

**Revisit trigger:** a radio model in esp-emu, the V1 board's antenna and matching, a
regulatory change for 433 MHz SRD, or a need for another band.

---

## ADR-0023 — V0.7 lab tools: software-verified behind one lab transport seam, physical electrical behaviour at V1; pin and bus safety

**Status:** accepted (2026-11-23). The owner delegated the V0.7 product decisions to the
planner and asked for autonomous progress; every decision below is recorded here rather
than taken silently. Applies ADR-0017 to V0.7 as ADR-0019 to ADR-0022 applied it to V0.3
to V0.6. STARK-0701 to STARK-0708 built it (all landed; the addenda at the end record the readings and
choices of each, the last restating the V0.7 budgets).

**Context.** ROADMAP V0.7 adds the everyday bench tools: a GPIO app with a safe-pin
allowlist, frequency and duty measurement without PCNT or MCPWM, an I²C scanner with
register access, an SPI probe, UART tools and a self-test with log export. Its three exit
criteria say "in a Wokwi scenario" or "in simulation". Since ADR-0017 nothing depends on
Wokwi, and its Free plan is not available to the project. What exists, verified:

* **The board has no spare pins.** HARDWARE §1 to §2 spend all 25 usable GPIOs. The only
  pins no peripheral owns are `LAB_UART_TX`/`RX` (38, 39) and, while no module is fitted,
  slot 0's `MOD_CS` 40 and `MOD_IRQ0/1` 41, 42 (HARDWARE §2.2, §6). Slot 1 has no GPIO.
* **esp-emu 0.44.0 models, per its README at the `v0.44.0` tag:** UART, USB Serial JTAG,
  GPIO, GDMA, GP-SPI, RMT, LEDC, PCNT, MCPWM and I²C "with a built-in EEPROM slave". Its
  options include `--uart1-tcp HOST:PORT` (UART1 as a TCP server for a host-side peer;
  upstream verified it with `uart_echo` on C3 and P4, not on S3) and `--rmt-loopback`. It
  has **no** `--i2c-dev` and no co-simulation option (both appear only in the v1.x
  documentation). No option drives an external GPIO pad. Verified earlier by this project
  (docs/VALIDATION.md §2, STARK-0502): the I²C bus ACKs 0x50 and reads zeros, and NACKs
  0x10, 0x24, 0x51, 0x52; GPIO inputs with pull-ups read released; the RMT loopback joins
  only TX 0 to RX 0, which IR owns; LEDC, buzzer and LED levels are not observable from
  outside. **Not verified, and not assumed:** an output pad read back as an input,
  pull-down reads, GPIO interrupts from a self-driven pad, LEDC reaching a pad, and the
  UART1 bridge on the S3 model. The planner could not run esp-emu in this session (the
  pinned binary is not installed and was not downloaded); STARK-0701 and STARK-0705 probe
  those behaviours first and record the result in docs/VALIDATION.md §2. No acceptance
  criterion below depends on an unprobed behaviour except where it says so.
* The V0.5 and V0.6 pattern works: a production driver over an L1/L3 transport seam, a
  test-only fake behind the seam in the emulator test image, pure cores with 100 % line
  coverage, and the physical half written down as a V1 exit and a blank bring-up document.

**Decision.**

1. **Two levels of completion.** *V0.7 complete* means **software / lab-protocol
   verified**: the pin allowlist and its guards, the GPIO, PWM and measurement logic, the I²C
   scan and register logic, the SPI probe rules, the UART logic including auto-baud and
   logging, the self-test and its report, the apps, storage, logging and error behaviour. It
   never means a pad was driven, a wire was measured or a real peripheral answered. Each
   exit criterion splits into a software half (V0.7) and a physical half that **becomes
   ROADMAP V1 exit 13** and `docs/bringup/v1-labtools.md` (written by STARK-0708, blank
   until V1). The three exit criteria that name "a Wokwi scenario" or "simulation" are
   reworded to the host and emulator evidence in ROADMAP V0.7; their intent is unchanged.
   Two exit criteria are added (frequency and duty measurement; self-test and export)
   because two scope items had none. Wokwi stays optional (ADR-0017) and no
   `sim/diagram.i2c.json` is created.
2. **One new seam, `stark_lab_transport`, the fifth and last allowed substitution of V0.7.**
   It carries four groups of calls and nothing else: pads (configure, level, hi-Z, edge
   capture, PWM), the shared I²C bus (probe, write, write-read), SPI transactions on a leased
   module slot, and the lab UART (open, read, write, change baud, close). Production is a
   thin wrapper over `stark_hal` (pads, PWM, UART), `stark_board_i2c_bus()` and
   `stark_module_spi`. The emulator test image substitutes it with one built on
   `test/fake/fake_lab.c`: a model of pads with an external source, an I²C bus with several
   devices at known addresses, SPI devices and a UART peer, all driven by pokes. One seam
   rather than four keeps `check_emu_image.py` at five substitutions, the fake in one file,
   and the layer table to one new transport component. **Reuse was considered and
   rejected:** `stark_module_transport` is read-only by construction (ADR-0021 §2) and one
   write call would remove that guarantee; `stark_subghz_transport` is the CC1101's.
   Production code has no fake mode and no simulator branch (ADR-0010). The production
   firmware needs **no new FreeRTOS task**: lab operations run on the app worker, and the
   UART driver is installed without an event queue. The raw GPIO and LEDC drivers stay in
   `stark_board` and `stark_hal` (`scripts/check.sh` `gpio-centralized` is unchanged);
   `stark_hal` gains the pad calls the transport wraps.
3. **Pin safety, owned by `stark_board`** (GPIO numbers stay there; SECURITY_SCOPE §4.6):
   * `stark_board` classifies every GPIO number, derived from the pin map and never
     hand-listed: `LAB` (38, 39: the lab UART pins, always offered), `SLOT` (a module slot's
     `MOD_CS` and `MOD_IRQ0/1`) and `RESERVED` (everything else, including every number
     outside 0 to 48). A `SLOT` pin is offered **only while its slot reads `ABSENT`** (no ID
     EEPROM answered). A `GENERIC`, `KNOWN` or `CONFLICT` slot, or any lease on it, withdraws
     its pins. Reserved pins are never offered, in no mode, not even to read: display, SD,
     buttons, buzzer, status LED, backlight, I²C, IR, USB, console, strapping and
     flash/PSRAM pins (HARDWARE §1, §2).
   * Enforcement is in the service, not only the UI: every pin call checks the offer, so a
     reserved pin returns `STARK_ERR_NOT_SUPPORTED` from the core, and again from the
     transport (defence in depth). A host test sweeps GPIO -1 to 64 against every call.
   * **Hot-plug:** the offer is recomputed on `STARK_SYS_MODULE`; a withdrawn pin goes to
     hi-Z at once and the owner is told (`lab: pin 40 withdrawn (slot 0)`).
   * **Drive needs an explicit arm.** An output, a PWM or a pulse is refused with
     `STARK_ERR_STATE` until the app has shown a confirm dialog naming the pin and
     "connected hardware may be damaged" and the user has pressed OK; the arm is per pin,
     lives in RAM, and is cleared by release, withdrawal and leaving the app. Before the
     dialog the app shows what the pin reads (floating, driven high, driven low), found with
     each pull for 2 ms; the dialog says so.
   * **Weakest drive strength** (`GPIO_DRIVE_CAP_0`, which ESP-IDF's driver documents as
     about 5 mA) for every lab output; the internal pulls are weak (about 45 kΩ, as noted in
     `board_devkitc1.c`).
   * **Contention is detected:** after an output is set, the pad is read back; a mismatch
     after 1 ms puts the pin in hi-Z, logs `lab: contention pin <n>` and tells the user.
   * **Default is input/hi-Z:** every pin is released to hi-Z (no pull) when the app stops,
     when a pin is withdrawn, and on a failed operation; PWM is stopped and the pad
     reconfigured as an input.
   * **V1 notes** (HARDWARE §2.4): 3.3 V logic only, not 5 V tolerant; the lab UART pins are
     series-protected and level-limited by design; the current into or out of any lab pin is
     bounded by the weakest drive and the V1 checks measure it. The board offers at most
     five pins; a lab header with spare pins is a V1 hardware requirement (ROADMAP V1
     hardware), not something V0.7 can invent.
4. **Frequency and duty without PCNT or MCPWM: timestamped edges.** A GPIO any-edge
   interrupt (in IRAM) stores `esp_cpu_get_cycle_count()` into a bounded ring of 1024
   entries owned by the app (4 kB of heap while the view is open); a pure core computes the
   frequency from the span of the rising edges and the duty from complete high intervals. A
   20 ms pre-gate counts edges to size the capture window so it fits the ring (10 ms to
   4 s). Range: **1 Hz to 50 kHz** measured; a faster signal reports "> 50 kHz, not
   measured" and a slower one "< 1 Hz"; a ring that overflows says so instead of returning a
   wrong number. Targets: frequency ±0.5 % up to 10 kHz and ±2 % to 50 kHz; duty ±2
   percentage points to 10 kHz and ±5 to 50 kHz. The host proves the arithmetic and the
   bounds on synthetic, jittered edge sets; **ISR latency and the real accuracy are V1
   measurements** (the V1 check wires `LAB_UART_TX` PWM into `LAB_UART_RX`).
   PWM out is LEDC through `stark_hal_pwm_*` on channel 2 (its own timer, since the HAL
   gives each channel one), 100 Hz to 40 kHz at the HAL's 10-bit resolution, duty 0 to 100 %
   in 1 % steps.
5. **I²C rules** (constants, never Kconfig): scan is address-only on 0x08 to 0x77, 112
   probes with a 5 ms timeout each, one sweep per request; speeds 100 or 400 kHz, default
   100; register reads of 1 to 16 bytes, a dump of at most 256 bytes after a confirm,
   because a read can change a device's state; **every write is confirmed** (address,
   register, bytes) and logged, 1 to 8 bytes; **writes are refused at the service** to the
   module ID EEPROM addresses (`stark_board` `module[i].eeprom_addr`, ADR-0021 §2: a
   descriptor is never written) and to the PN532's address while NFC is confirmed (its
   driver owns the protocol); no write loops, no repeat, no sweep-write. The hints table is
   at most 48 generic public addresses with "may be" wording, data only, no probing of
   identity registers, no secrets, no vendor database.
6. **SPI probe rules:** only on a **leased module slot**, through `stark_module_claim()` and
   `stark_module_spi`; only slots whose descriptor declares `MOD_SPI` and **no function
   bit** (a "SPI breakout" module, catalogue entry 34, `0x08`). A slot declaring `SUBGHZ` or
   any other function is refused (`owned by <driver>`): raw SPI to a CC1101 could write
   `PATABLE` or strobe `STX` and walk around the transmit policy (ADR-0022 §6), so the
   probe never touches a driver-owned module. One transaction of 1 to 32 bytes per
   confirmation (the dialog shows the bytes, slot, clock and mode), clock 100 kHz to 4 MHz
   (default 1 MHz), mode 0 to 3, no repeat or stream, every transaction logged with MOSI
   and MISO. No new capability bit: the probe is a tool on wiring, not a function a driver
   confirms (ADR-0021 §2); the app reads the module table itself, as Modules does.
7. **UART rules.** UART1 on the lab pins (`LAB_UART_TX/RX`, 38, 39), 3.3 V TTL only.
   Baud 1 200 to 921 600 from a fixed list; formats 8N1, 8E1, 8O1, 8N2, 7E1. Monitor: a
   4 kB RAM ring of received bytes, hex and ASCII views, framing and parity errors counted,
   overrun counted and shown (never silently dropped). Send: 1 to 32 bytes from a nibble
   editor, a short canned list, or a file `uart/send.txt` of at most 1 kB; one send per key
   press, no repeat. **"Bridge" is defined as the loopback bridge**: the device sends a
   known 64-byte pattern out of TX and verifies the identical bytes back on RX (with TX
   wired to RX, or through a peer that echoes), at the chosen baud, reporting bytes, errors
   and the first mismatch; ROADMAP exit 2's intent ("data round-trips at 115200") is met.
   **A USB-CDC bridge to a host is not built:** GPIO 19/20 are reserved and USB classes are
   out of scope until V2 (PROJECT.md out-of-scope list). **Auto-baud** is a pure core over
   the same edge capture: the shortest pulse width on RX, snapped to the nearest standard
   baud within 3 %, else "unknown". **SD logging** writes the received stream to
   `logs/uart_NNNN.log` through a 2 kB buffer on the app worker, at most 1 MiB per file, then
   stops and says so.
8. **Self-test** is a pure core in `stark_diag` over an injected check table, registered at
   the composition root (`main/selftest_checks.c`) so `stark_diag` needs no new edge.
   Automated: internal RAM (bounded pattern tests on one block of at most 32 kB that never
   takes free heap below 128 kB; PSRAM stays unused, ADR-0014), flash (two read-only SHA-256
   passes over the running image must agree, and the chip size must match the image header;
   nothing is erased or written), settings (NVS reads back), storage (if a card is mounted: a
   4 kB write, read-back and delete), module bus (the table agrees with address probes of
   the slot EEPROMs), event and stack sanity. **Operator-attested** (the device cannot see
   or hear itself): display colour bars, buttons (each key pressed in turn), buzzer, status
   LED; each is recorded as `attested`, never as `pass`. **Log export** writes
   `logs/report_NNNN.txt` (at most 16 kB: firmware version, board, uptime, `stark_diag`
   snapshot and tasks, module table, storage state, every self-test result with its
   evidence, and the tail of the log ring); without a card it is shown and logged only.
   The report stays on the device (SECURITY_SCOPE §2: "Self-test, diagnostics, measurement,
   logging, export"); nothing is sent anywhere.
9. **Emulator evidence is integration evidence.** *Production image:* the I²C app finds
   exactly 0x50 and reads zeros there through the real driver (the facts of V0.5), a write
   there is refused, the GPIO app offers exactly 38 and 39 (slot 0 reads generic), a
   pull-up read of an unconnected pin is high, and, if the STARK-0705 probe shows the UART1
   bridge works on the S3 model, a host-side peer round-trips bytes through the production
   UART driver. *Test image:* the fake carries the multi-device bus, an external source on
   a pad, a wire between two pads, a UART peer, SPI devices and faults. The emulator never
   shows a pad moving, a wire carrying a signal or a peripheral answering; the fake is
   spec-derived and never a recording. Harness additions (not firmware): `test_emu.py`
   gains the poke names and an optional `uart1_peer` for the bridge scenario.
10. **Budgets and gates.** Production boot heap **≥ 200 kB** (current 240 732 B) with a V0.7
    ceiling of **16 kB** of boot-heap cost in total (closing figure ≥ 224 kB); per-task
    limits are in each task. Binary 483 216 B now; ceiling **+52 kB** (≤ 535 kB of the 4 MB
    partition). Test image boot heap 237 812 B now. Transient buffers (4 kB edge ring, 4 kB
    UART ring and driver, the RAM-test block) are heap, held only while a view runs, and
    freed on exit like STARK-0603's capture buffer. Heap after leaving every app is within
    1 kB; drops 0.
11. **LVGL re-evaluation (ADR-0005's V0.7 trigger).** Scored at the end, from the apps
    actually built: a hex/ASCII monitor is scrollable text (one trigger); there is no chart
    widget, animation or on-screen keyboard (the nibble editor and fixed lists are enough).
    Provisional outcome, to be confirmed by STARK-0708: **keep the in-house UI**, since fewer
    than two triggers are met; the scoring is a STARK-0708 deliverable recorded as an ADR-0005
    addendum.
12. **Task order** (dependency order, each the size of a V0.6 task): 0701 pin safety, the lab
    seam and the fifth substitution; 0702 GPIO app; 0703 frequency and duty; 0704 I²C
    scanner; 0705 UART tools; 0706 SPI probe; 0707 self-test and export; 0708 exit.
    Launcher rows, events and pokes are in TASKS.md.
13. **Scope bound.** These are bench tools for our own hardware (SECURITY_SCOPE §1 to §2). No
    tool drives a pin, bus or module it does not own, no tool bypasses another owner's
    policy (the transmit limits, the module EEPROMs), no tool loops unattended, and nothing
    is a database of third-party devices, codes or credentials.

**Alternatives rejected**

| Option | Why rejected |
| --- | --- |
| Four seams (pads, I²C, SPI, UART) | Four substitutions and four fakes for one milestone; one transport with four call groups is one substitution and one fake |
| Extending `stark_module_transport` with writes | Removes the "read-only by construction" guarantee of ADR-0021 §2 |
| Relying on esp-emu alone (no fake) | It cannot drive a pad, has one I²C device, and has no SPI device; the multi-device and wire evidence would be missing |
| Waiting for `--i2c-dev` (esp-emu v1.x) | Not in the pinned 0.44.0; a version bump is its own change (`esp_emu.lock`), and one register-file device would still not be several known devices wired to our own scenarios |
| PCNT or MCPWM capture for frequency | The roadmap excludes them; PCNT cannot see duty, and the emulator cannot stimulate either |
| Offering the status LED, buzzer, IR or other owned pins | Their drivers own them; the guard is that no lab call can reach a pin another owner has |
| Offering slot pins on `GENERIC` slots | A slot with an unreadable ID may still hold a chip whose outputs would fight ours |
| Raw SPI to any slot, including a radio | Bypasses the transmit policy (ADR-0022 §6) |
| A USB-CDC to UART bridge | USB pins are reserved and USB classes are out of scope until V2 |
| A new capability bit for the lab apps | Nothing a driver confirms; the pins are onboard, the slot rules are checked in the probe |
| Treating operator-attested checks as `pass` | A device cannot see its own panel; claiming it would be a false green |
| A persistent pin "profile" in NVS | Flash wear and a risk of driving a pin at boot; the state is RAM and starts hi-Z |

**Consequences.** Five apps join the launcher: System gains Self-Test; Lab gains GPIO, I2C,
SPI and UART (titles sort `GPIO`, `I2C`, `IR`, `NFC`, `SPI`, `Sub-GHz`, `UART`).
`stark_board` gains `lab_uart_tx/rx`, pin classes and validation; `stark_hal` gains pad
configuration, hi-Z, edge capture and PWM stop (with its host fake); a new component
`stark_lab` (L2 cores, L3 service) and `stark_lab_transport` (L3 port, uses `stark_module`'s
lease). System event ids: 8 `STARK_SYS_LAB`, 9 `STARK_SYS_SELFTEST`. `check_emu_image.py`
allows exactly five substitutions. The module catalogue gains entry 34, the SPI breakout.
The V0.7 report separates software, fake-device, host, emulator and deferred physical
evidence. `docs/bringup/v1-labtools.md` is written by STARK-0708 and filled in at V1.

**Revisit trigger:** an esp-emu with external pad stimulus or a multi-device I²C model
(`--i2c-dev`, co-simulation) which could replace parts of the fake, the V1 board's lab
header, the first real measurement of ISR latency, or a need for a USB bridge.

**Addendum (2026-11-24, STARK-0701).** The emulator behaviours the Context left unprobed were
run on the pinned esp-emu 0.44.0 (a throwaway probe task on the production image; every row is
in docs/VALIDATION.md §2). *Works:* an output pad reads back as an input; a pad the firmware
toggles itself raises its GPIO interrupt (20 of 20 edges); the I²C sweep over 0x08 to 0x77
finds exactly 0x50. *Does not:* **pull-down reads** (an input reads high with a pull-down, a
pull-up or no pull on every unconnected pad tried, and a pad released after being driven low
reads high too) and **LEDC reaching a pad** (a pad routed to LEDC reads low at every duty).
*A correction:* the built-in EEPROM at 0x50 **accepts writes** and returns them, so STARK-0704's
refusal of writes to it (§5) is firmware's alone; its production-image scenario checks that a
refused write left the register at zero. Consequences for the later tasks: the pad-state probe
(§3) reports every unconnected pad on the production image as driven high, and PWM output and
its measurement (§4) are proven on the host and the test image only; no acceptance criterion
may depend on either in the production image. The decisions STARK-0701 took where the plan
was silent, each the smallest sound choice and none changing a rule above:

1. **Board API.** `stark_board_lab_pins()` copies into a caller buffer instead of handing out a
   static table (no lazy initialisation to race), and every pin function has a map-taking form
   (`stark_board_pin_class_of()`, `stark_board_lab_pins_of()`) so the host tests can move a pin.
   Names are board-neutral (`MOD0_CS`, `MOD0_IRQ0`, `MOD0_IRQ1`): slot 0's lines are
   `SUBGHZ_*` in HARDWARE §2.2, but the lab does not know what a slot carries.
2. **The transport guards slot lines too.** `stark_lab_pad_ok()` reads the module table, so
   "again from the transport" holds for a slot pin and not only for the static class; the
   layer edge `stark_lab_transport -> stark_module` therefore exists now rather than at
   STARK-0706. One call is exempt: `pad_hiz` accepts any classified pin, because withdrawal must
   be able to release a pin that has just stopped being offered.
3. **Hot-plug.** Every call checks the *live* offer, and the refresh runs from
   `main/module_bridge.c` on `STARK_SYS_MODULE` (the apps may call it too), so the hi-Z and the
   log happen with no app open. The service logs `lab: pin <n> withdrawn (slot <s>)` for every
   pin that leaves the offer, held or not (the hi-Z is for held pins), and `lab: pins <list>`
   whenever the offer changes; `STARK_LAB_EVT_PINS` is published on each. An `ABSENT` slot
   cannot hold a valid lease (a claim needs `KNOWN`, removal invalidates it), so "unleased" needs
   no separate query.
4. **A failed drive also clears the arm** (contention, a transport error), not only release,
   withdrawal and leaving the app: after a contention the user confirms again.
5. **Pad state.** Pull-up high and pull-down low is floating; high with both is driven high; low
   with both is driven low; low with the pull-up and high with the pull-down is `STARK_ERR_IO`
   (the pad moved while it was looked at) and ends in hi-Z.
6. **Small facts.** Owner names are cut to 15 characters. `stark_lab` has no Kconfig options
   (every limit is a constant), so it has no `Kconfig.stark`. No `emu_lab_*` poke is added: nothing
   drives a pad before the GPIO app (STARK-0702), which brings them with it.
7. **HAL pad hygiene (ESP-IDF).** `gpio_config()` reserves an output pad and warns `gpio:
   conflict found` on every later output configuration, and LEDC refuses a reserved pad;
   `gpio_reset_pin()` revokes. `stark_hal_gpio_*` therefore tracks the pads it made outputs,
   sets only the level when a driven pad is driven again, and resets the pin on any other
   configuration. STARK-0702 relies on this before it routes a pad to LEDC.

**Addendum (2026-11-24, STARK-0702).** The GPIO app and the PWM and pulse outputs. The plan was
silent or ambiguous on the points below; each is the smallest sound choice and none changes a rule
above. The emulator probes of the 0701 addendum still hold: no acceptance criterion depends on
LEDC reaching a pad or on a pull-down read in the production image, so PWM is proven as arithmetic
on the host and as a request on the test image, and its waveform is a V1 logic-analyser check.

1. **Keys.** The plan's "one decade steps up/down on UP/DOWN" cannot be: UP and DOWN move the
   selection in every `ui_menu`, and the Mode, Pull, Level, Freq, Duty and Width rows are
   adjusted with LEFT and RIGHT like every other value row (press and repeat). The frequency row
   moves by the leading digit of its decade (100, 200, … 900, 1000, … 9000, 10000, … 40000: 22 rungs
   in 21 steps), so a decade is one press away from a rung and every decade is crossed; duty
   moves by 1 %; width through the six fixed widths. `lab_pwm_step_hz()` is pure and swept.
2. **The home list is always `Hi-Z`.** BACK from a pin releases it, so the app holds no pin at
   home: the state column reads the service (`stark_lab_info()`), which says "not held". The
   live state is the pad row inside a pin's view (`IN H`, `IN L`, `OUT H`, `OUT L`, `PWM`, `Hi-Z`),
   refreshed after every action and on a mode change; an input's level is read when the view
   refreshes and on the Read row, because no timer runs. The pad row carries the pin number and name
   (the status-bar title only repaints when a screen is pushed, so it stays "GPIO").
3. **Pin view.** Mode (Hi-Z, Input, Output, PWM, Pulse) is the first row. Changing it releases the
   pad first (which stops a PWM); an input applies its pull at once (a read is not a drive).
   Hi-Z has a Probe row (floating, high or low: `stark_lab_state()` then released again), Input a Pull
   and a Read row, Output a Level, Drive and Toggle, PWM a Freq, Duty and Start (Stop while it
   runs), Pulse a Level (the pulse's polarity, which the plan left out), a Width and Pulse. A setting
   never drives: only an action row does. Toggle flips the Level row and drives it.
4. **The confirmation** runs `stark_lab_state()` for "reads: floating | high | low", releases the
   pad again, and asks. OK arms the pin for the session and goes on with the action; BACK leaves
   the pad in hi-Z and logs `gpioapp: cancelled pin <n>`. A failing probe is a log line and no
   dialog. Stopping a PWM needs no confirmation.
5. **Service additions** beyond the plan, each small: `stark_lab_hiz(pin)` (release the pad, keep
   the pin and its arm: the Mode row needs it), `stark_lab_info(pin)` (mode, pull, output level,
   PWM values, arm: tracked by the pure core so the app never guesses) and
   `stark_lab_pulse_cancel()` (no lock). `stark_lab_pwm_start()` on the pin that already has the
   PWM restarts it with the new values, which makes Freq and Duty live while it runs; a second pin is
   `STARK_ERR_BUSY`. The PWM's stop keeps the arm. Every call that puts the pad in another mode
   (input, hi-Z, drive, probe, pulse) stops the PWM first, and so does every exit path.
6. **Pulse.** The pulse is the service's: the idle level (the opposite of the pulse's) is driven
   and read back after 1 ms, so a pad that does not follow it is a contention before anything is
   sent; the transport then drives the level for the width and the opposite level again; the service
   restores the mode the pad had (hi-Z, an input with its pull, or an output at its level, read
   back). A short to the *idle* level cannot be seen by the read-back and is left to the V1
   measurement; it is not claimed. The wait is the transport's: under 1 ms a busy wait (`esp_rom_delay_us`,
   via the new `stark_hal_delay_us()`), otherwise `stark_hal_delay_ms()` in slices of at most 10 ms.
   The pulse runs on the app worker and holds the service's mutex for its width (up to 1 s), and the
   worker join timeout is also 1 s, so the wait must end early: `stark_lab_pulse_cancel()` (a
   volatile flag, no lock) is called by BACK, by the screen's `on_exit` (which the manager calls
   before it joins the worker) and by `stark_lab_refresh()` (so a hot-plug never waits behind a pulse).
   A cancelled pulse still ends at the opposite level and the mode is restored. While a pulse runs
   every key but BACK is swallowed: one pulse per key press.
7. **PWM details.** The PWM is not read back (a square wave has no one level), so a pad driven by
   something else is not detected; the confirmation, which shows what the pad reads, is the guard.
   LEDC channel 2 with its own timer, 10-bit resolution, `stark_hal_pwm_set_duty_pct()` unchanged
   (the buzzer's and the backlight's channels are never named by the lab, and a host test pins
   that): duty 100 % is 1023 of 1024 ticks, a low glitch of 0.1 % per period, inside the ±2 point
   accuracy ADR-0023 §5 allows. `lab_pwm_plan()` models the divider (80 MHz APB, Q8, valid 1.0 to
   1023.996) and is the production validity check: every integer frequency from 100 to 40 000 Hz is
   in range with a worst error of 997 ppm (39 960 Hz), bounded at 0.2 % by construction (half a
   divider step at a divider of 1.0); ESP-IDF's own clock choice may do better. The weakest drive
   strength of a pad that LEDC drives is set after the routing (`stark_hal_gpio_drive_weak()`), and a
   pad routed to LEDC is tracked with the outputs, so the next `config_in`/`hiz` resets it.
8. **Contention and a transport fault are both `STARK_ERR_IO`** (the 0701 contract), so the app's alert
   ("Contention": the pin did not follow what it drove, released) covers both; the service's own
   `lab: contention pin <n>` line says which. Every failure ends in hi-Z with the arm cleared.
9. **Logs (contract).** App: `gpioapp: home <pins>`, `open pin <n> <name>`, `pin <n> <state>` on
   a change, `pin <n> pull <none|up|down>`, `pin <n> read <H|L>`, `pin <n> probe <…>`,
   `confirm pin <n> reads <…>`, `cancelled pin <n>`, `pin <n> drive failed: released`, `pin <n>
   withdrawn`, `close pin <n>`. Service: `lab: armed pin <n>`, `pwm pin <n> <hz> Hz <duty> %`,
   `pwm stopped`, `pulse pin <n> <high|low> <us> us`, `released pin <n>`, `released all`.
10. **Test image.** The pokes are `emu_lab_ext_pin` (default 38), `emu_lab_ext_mode` (0 none, 1 high,
    2 low, 3 square), `emu_lab_ext_hz`, `emu_lab_ext_duty`, `emu_lab_wire` (`from * 64 + to`) and
    `emu_lab_fault` (a `fake_lab_fault_t`, any pad, until it changes). With a wire in place a PWM start
    is followed by the model's own check (the destination pad sampled 1000 times over four periods):
    `lab: wire 38 -> 39 edges=4 high=25%`. The scenario is split in two (`storage/v07-gpio`,
    `storage/v07-gpio-pwm`) to stay inside the 20 s budget. `check_emu_image.py` still allows five
    substitutions; the fake adds no sixth.

**Addendum (2026-11-24, STARK-0703).** Frequency and duty from timestamped edges. The plan was silent
or ambiguous on the points below; each is the smallest sound choice and none changes a rule above.
The probes of the 0701 addendum still hold (a self-driven pad raises its interrupt, an unconnected
pad does not, LEDC does not reach a pad): no acceptance criterion depends on a pad that moves in
the production image, so the arithmetic is proven on the host against an independent Python
reference, the edges on the test image over the pad model, and the real interrupt only as far as
"installed, counted nothing, removed" on the production image. ISR latency and the accuracy of a
real signal stay V1 measurements.

1. **IRAM placement (the open question of the 0701 finding).** The edge handler is `IRAM_ATTR` and the
   interrupt service is installed with `ESP_INTR_FLAG_IRAM`. Reason: a flash operation in the middle of
   a window (a settings write, a log file) would otherwise delay a stamp by milliseconds, a gap that is
   indistinguishable from a slow signal, and the result would be a wrong number, which §4 forbids. The
   cost is the handler's own 84 B: the default IRAM was already full (16 384 of 16 384 B, 0 B free), so
   the linker places it in the instruction region of DIRAM and the heap shrinks by about that much (the
   measured effect on the boot heap is in docs/measurements.md, "V0.7 lab tools", inside the 0.5 kB
   the task allows). A non-IRAM handler was rejected because its failure is silent.
2. **The handler switches itself off.** Once `limit` edges have come (the ring's capacity plus one, so an
   overflow is known, or 4 096 for the pre-gate that has no ring) the handler masks its own interrupt
   with the register-level `gpio_ll_intr_disable()`. A signal far above 50 kHz (a 10 MHz clock on a lab
   pin) would otherwise keep the CPU in the handler until the interrupt watchdog fired. The driver's own
   `gpio_intr_disable()` is not IRAM-safe without `CONFIG_GPIO_CTRL_FUNC_IN_IRAM`. ESP-IDF's
   `gpio_ll.h` does not compile under our `-Wconversion -Wsign-conversion -Werror` (its inline
   functions narrow into register bit fields), so `stark_hal_esp.c` includes it inside one documented
   `#pragma GCC diagnostic push/pop`: third-party code, the only pragma in the tree, explained at the
   include.
3. **HAL signature.** `stark_hal_gpio_edges(pin, window_us, ts, cap, *n, *seen, *first_level)`: `*seen`
   (edges counted, up to `cap + 1`, or 4 096 when `cap` is 0) replaces the plan's `overflowed` flag,
   because the pre-gate has to count and the ring has to say that it overflowed with one call shape;
   `seen > cap` is the overflow. `*first_level` is read just before the interrupt is enabled. The
   wait sleeps in 10 ms slices while the window is long and busy-waits the last 20 ms, so a 10 ms
   window is 10 ms and not 11; it ends early when the ring is known to have overflowed, and on a
   cancel (`STARK_ERR_STATE`). The GPIO interrupt service stays installed after the first
   measurement (one allocation of 49 handler slots, in the heap); every window adds and removes the
   pad's handler. `stark_hal_tick_hz()` is the CPU clock from `esp_rom_get_cpu_ticks_per_us()`.
4. **The ring is the app's, per measurement.** The plan said the app allocates it when the Measure view
   opens. The app allocates the 4 kB on the worker for one measurement and frees it before it
   announces the result, so nothing is held between measurements and no exit path can leak it. The
   service allocates nothing and takes `ring, cap`; a ring longer than 1 024 is cut to 1 024, one
   shorter than 2 is refused.
5. **The rules the plan left to the arithmetic** (all in `lab_meas.h`, restated by the Python
   reference): a step between stamps is the unsigned 32-bit difference and 2^31 or more is a stamp
   that went backwards (`INVALID`: one more status than the plan listed, shared with a zero clock and
   a ring longer than 1 024); a pulse under 1 µs, a zero step included, is a `GLITCH`; fewer than two
   rising edges is `TOO_SLOW` (a window shorter than two periods); no edge at all is `NO_SIGNAL` and
   carries the level the line sat at; an `overflowed` ring is `OVERFLOW` whatever the stamps say. The
   range limits carry a guard so a signal at the limit, read with its own jitter, is not refused:
   `TOO_FAST` above 50 kHz + 0.1 %, `TOO_SLOW` below 0.99 Hz (one count of the 0.01 Hz unit). A signal
   outside the range is never a number: frequency, duty and period are zero unless the status is `OK`.
6. **Window sizing.** The gate counts edges for 20 ms; `lab_meas_plan()` sizes the window for about 768 of
   the 1 024 stamps (`768 * 20 ms / edges`), clamped to 10 ms to 4 s, and 4 s when the gate saw
   nothing. At the top of the range that is the 10 ms floor: 1 000 to 1 002 edges for 50 kHz to
   50.05 kHz, inside the ring with 22 stamps to spare, which is why the gate refuses more than
   50.05 kHz at once (3 200 edges at 80 kHz) and no capture follows. The host sweeps every gate count
   from 1 to 4 096 against the ring.
7. **After a measurement the pad is what it was**: an input with its pull, mode `IN` in
   `stark_lab_info()`. "The pin is released" in AC 3 is read as the edge interrupt (the only thing a
   measurement adds) being gone; release to hi-Z stays what BACK, leaving and withdrawal do. A
   transport failure ends in hi-Z with the arm cleared (the 0701 contract); a cancel
   (`STARK_ERR_STATE`: BACK, `on_exit`, the hot-plug refresh) leaves the pin as it is. The transport
   re-checks the pin's class only: the plan said "an input-configured pin", but the service already
   requires the pin held in mode `IN`, and the transport has no memory of modes.
8. **The cancel is a per-capture flag**, like the pulse's: cleared when a capture starts, set by
   `stark_lab_measure_cancel()` (no lock). A cancel that arrives between the gate and the window is
   therefore lost and the measurement ends within 4 s instead of 10 ms; BACK has released the pin by
   then, so the cost is a delayed return of the worker, not a wrong state. The app swallows every key
   but BACK while it waits.
9. **The Measure row** is the fourth row of the Input view (Mode, Pull, Read, Measure, Pad); its value is
   "50 kHz max" until there is a result, then the result line (`1.000 kHz duty 25.0 %`,
   `250.00 Hz duty 50.0 %` below a kilohertz, `> 50 kHz, not measured`, `< 1 Hz`,
   `no signal (high)`, `overflow: too many edges`, `glitches (< 1 us)`, `invalid timestamps`),
   cleared by a change of pull or mode. The log form is `lab: measure 1000.0 Hz duty 25.0 %`,
   `too fast`, `too slow`, `no signal (high)`, `overflow`, `glitch`, `invalid`.
   `STARK_LAB_EVT_MEASURE_DONE` (2) is published with every result; the app is told by its own
   worker note, which arrives after the result is stored.
10. **AC 4 and the one-pin app.** The GPIO app holds one pin and BACK from a pin releases it (0702), so a
    PWM on pin 38 cannot keep running while pin 39 is measured: the emulator cannot show a PWM wired
    to a measured pad through the app. The host proves exactly that path (`test_lab_pins`: PWM on 38,
    wire 38 to 39, pin 39 measured at six frequency and duty pairs, a gate and a window each) and the
    test image measures an external source on pad 39 through the app (`storage/v07-measure`), over the
    same pad model and the same service. The V1 check "`LAB_UART_TX` PWM into `LAB_UART_RX`" needs two
    live pins, which the app cannot give; it is STARK-0708's to restate (a signal generator, or the
    UART tool's own loopback pattern), not decided here.
11. **The pad model's edges.** `fake_lab_pad_edges()` gives the stamps of an external source, a wire or the
    pad's own PWM at 240 ticks per microsecond from the model's clock, with the period `240 MHz / hz`
    in whole ticks (error at most `hz / 240 MHz`), optional jitter (a deterministic function of a seed
    and the edge's number, kept in order), glitch pairs and a counter epoch (a window across the
    32-bit wrap). The test image's transport stamps from the emulated clock and waits the window out
    in slices, like the hardware. No new poke is needed (`emu_lab_ext_*` and `emu_lab_wire` carry the
    sources), so `check_emu_image.py` still allows five substitutions.
12. **The vectors.** `scripts/gen_lab_vectors.py` has its own reference implementation of the rules in
    `lab_meas.h` (exact rational arithmetic, the counter unwrapped into integers, edges as (time,
    level) pairs) and 79 edge sets: every frequency of the catalogue (1, 2, 10, 100 Hz, 1, 10, 25,
    50 kHz) at six duties with seeded jitter of up to ±2 µs (never more than a quarter of the shortest
    pulse, so it cannot reorder edges), the pairs whose shortest pulse is about a microsecond, which
    the reference itself reports as `GLITCH`, the limits, a steady line, one and two edges, an
    overflow, a counter wrap, a glitch train and hostile input. Generation fails if the reference's
    result for a measured vector (49 of them) is outside the accuracy of §4 (±0.5 % to 10 kHz, ±2 % above;
    ±2 and ±5 points of duty). The jitter is zero-mean and seeded: a systematic bias of the real
    interrupt (a latency that differs between the rising and the falling edge) is a V1 measurement.

**Addendum (2026-11-24, STARK-0704).** The I²C scanner. The plan was silent or ambiguous on the points
below; each is the smallest sound choice and none changes a rule above. The probes of the 0701 addendum
still hold (the sweep finds exactly 0x50; the emulator's EEPROM accepts a write, so the refusal is the
firmware's alone) and no acceptance criterion depends on more than they show.

1. **The refusal rule is one table, stated three times and tested against a fourth.** *A module ID EEPROM
   takes a one-byte pointer and a read, nothing else; the PN532 takes nothing while NFC is confirmed.* The
   pure core (`lab_i2c_refusal()`, with the reason `module eeprom` or `nfc reader`), the transport's guard
   (`stark_lab_i2c_allowed()`, an inline function in `stark_lab_transport.h` that the production file, the
   test image's transport and the host shim all call) and the independent Python table in
   `scripts/gen_lab_vectors.py` (112 vectors: seven addresses, eight transaction shapes, NFC on and off) give
   the same answer; the core's answer is the service's, the guard's is "again from the transport" and refuses
   before the bus is touched. Two readings go beyond the plan's wording: **a 16-bit register read of a module
   EEPROM is refused**, because the two bytes before the repeated start are a pointer and a data byte, which
   is a write to the chip (the model's 0x68 does exactly that); and **a read of the PN532 is refused as well
   as a write** while NFC is confirmed, because a status read there consumes the driver's frame (the fake
   PN532 models it). Without NFC confirmed the PN532 is an ordinary address. The scan itself is address-only
   and is never refused: it must be able to list them.
2. **The I²C group is a target-only file and a host shim.** The production transport gains
   `lab_transport_i2c.c` (ESP-IDF's I²C master driver, `PRIV_REQUIRES esp_driver_i2c`): a probe is
   `i2c_master_probe()` on `stark_board_i2c_bus()`; a transaction attaches a device handle for itself at the
   requested clock and removes it again, so no handle outlives a call (the driver keeps no duplicate check,
   verified in the IDF source, and applies each handle's own clock to its own transactions). `scripts/check.sh`
   `gpio-centralized` is unaffected (`driver/i2c_master.h` is not `driver/i2c.h`, and `stark_hal` still wraps
   no I²C). Because the file needs the driver it cannot be host-built like `lab_transport_hal.c`; the host
   builds `test/host/support/lab_transport_i2c_host.c`, the same guard in front of the pad model's bus. The
   fifth substitution stays the last: the test image's transport carries the I²C group too, and
   `check_emu_image.py` still allows five.
3. **One bus in the test image.** `fake_lab.c` models five devices (0x23, 0x3C, 0x48, 0x68, 0x76, specified in
   `fake_lab.h`) and delegates every other address to hooks (`fake_lab_i2c_ext()`); the test image binds them
   to the fake module transport (0x50 to 0x57, the 24C02 behind the slots) and the fake NFC transport
   (0x24), so one scan shows one consistent bus. **Consequence for AC 1:** the fake PN532 is on the bus from
   boot, so a scan on the test image lists `0x23 0x24 0x3c 0x48 0x68 0x76`; "exactly the five" holds when the
   NFC fake is switched off (`emu_nfc_fault = 1`, which is how `storage/v07-i2c` shows it) and, on the host,
   with no hook bound. The poke `emu_lab_i2c_devs` (a bitmask, `0x1F` at boot) selects the five;
   `emu_lab_i2c_fault` maps 1 to NACK_ALL, 2 to a timeout at 0x48 only (the sweep goes on without it), 3 to
   STUCK (every call times out) and 4 to DROP_DEVICE at 0x68 (it answers once and vanishes). The model's
   pointer semantics follow real parts: the first byte written is the pointer, the following bytes are stored
   from it, reads continue from it, both wrap at 256.
4. **The sweep and its errors.** An ACK or a NACK is an answer; anything else (a timeout, a bus error, a
   missing bus) is an error, three in a row end the sweep with `STARK_ERR_IO` and the list so far, and
   `stark_lab_scan_t` carries `errors` (the count of failed probes, which the log shows as `(2 errors)` when
   the sweep still finished). A NACK storm is 112 probes and an empty list, never an error. The call is
   synchronous on the caller's task (at most 112 probes of 5 ms), so `STARK_LAB_EVT_SCAN_DONE` is **not
   added**: the return is the completion, and the id stays reserved in the event list. The I²C group has its
   own mutex, so a dump never holds up a pin; no task is added.
5. **The arm is a fingerprint, and any call spends it.** `lab_i2c_arm()` stores a 32-bit FNV-1a of what the
   dialog showed (`'W'`, address, width, register, count, bytes; `'D'` and the address for a dump; the speed is
   not shown and so not part of it). The next `write` or `dump` takes it whatever it returns: success, a bus
   error, an invalid argument, a refusal or a different payload all spend it, so a failed confirmation never
   leaves a live arm behind. An arm is stale after 30 s (the clock is injected, the wrap is handled), a later
   arm replaces an earlier one, and `stark_lab_i2c_disarm()` clears it (BACK out of an editor, the app
   stopping). The order inside the service is arguments, then the refusal, then the arm, so a refusal needs
   none and a refused call is `NOT_SUPPORTED`, not "not armed". A dump is armed like a write (§5: "after a
   confirm"). `stark_lab_i2c_refusal(addr, reg16, write)` lets the app say so before it asks; the app then
   **still makes the call, unarmed**, so the service refuses it and logs `lab: i2c write refused: module
   eeprom` on every build: the rule is demonstrably the service's and not only the app's.
6. **The editor is rows, not a nibble cursor.** UP and DOWN move the selection in every `ui_menu` (the 0702
   addendum, point 1), so the register, the byte count and each byte are rows adjusted with LEFT and RIGHT,
   and a *Nibble* row says which nibble that is (high or low); a nibble wraps inside itself, so every byte is
   at most 16 presses from any other (`test_i2c_views`: all 256 values). The register and nibble carry over
   from the Read editor to the Write editor and back within one device (write a register, read it back, no
   setting up again) and reset when another device is opened; a result is forgotten when the register moves.
   The selection follows the row, not the index, when rows come and go (a result, a 16-bit register's high
   byte, the byte rows). A 16-bit register's high byte is its own row (*Reg high*).
7. **The hints table** is 31 entries of address and the whole phrase, sorted, in "may be ..." wording, at most
   30 characters so a phrase fits a row beside its address; it holds the examples the plan gave in shorter
   words. `stark_lab_i2c_hint()` is a lookup and probes nothing. 0x50 and 0x51 say "may be EEPROM or a module
   ID", 0x24 "may be NFC reader (PN532)".
8. **Logs (contract).** Service: `lab: i2c scan found 0x23 0x3c ...` (`none`; the first 16 and `+n`; `(n
   errors)`), `lab: i2c scan bus error found <...>`, `lab: i2c read 0x68 reg 0x75 68` (the bytes), `lab: i2c read
   refused: <reason>`, `lab: i2c dump 0x68 ok` / `<error>` / `dump refused: <reason>`, `lab: i2c write 0x68 reg
   0x6b 01 ok` / `<error>`, `lab: i2c write refused: <reason>`. App: `i2capp: home`, `scan <n> found`, `scan bus
   error after <n> found`, `open 0x68`, `read 0x68 reg 0x75 len 1: 68`, `read ... failed: <err>`, `confirm
   write 0x68` / `confirm dump 0x68`, `cancelled write 0x68`, `write 0x68 reg 0x6b: 01 ok`, `dump 0x68 ok`,
   `Write refused: module eeprom` (the same for Read and Dump), `speed 400 kHz`.
9. **Evidence.** *Production image* (`v07-i2c-prod`): the scan lists exactly `0x50` (so 0x10, 0x24, 0x51 and
   0x52 NACK), a read of 0x50 register 0 returns eight zeros, a write to it is refused with no confirmation and
   no write logged, and a read after it is still zero; docs/VALIDATION.md §2 records the whole list. *Test
   image:* `storage/v07-i2c` (five devices, WHO_AM_I, a cancelled and then a confirmed write that reads back,
   a second press that asks again) and `storage/v07-i2c-bus` (a module and the PN532 on the same bus, the
   refusals of both, a dump behind a confirmation, a stuck bus ending the sweep with `Bus error`, recovery).
   *Host:* `test_lab_i2c` (the sweep over 15 vectors, the bus and its faults, every refusal, the arm, lengths at
   each edge, fingerprints, hex and the hints), `test_i2c_views`, `test_ui_i2c` (33 frames pinned after
   inspection), `test_fake_lab`, and the independent Python reference for the sweep, the fingerprint (FNV-1a),
   the refusal table and the hex text; `lab_i2c.c`, `lab_i2c_hints.c`, `i2c_views.c` and `fake_lab.c` run at
   100 % lines. Nothing here shows a device answering on a wire (ROADMAP V1 exit 13).

**Addendum (2026-11-24, STARK-0705).** The UART tool. The plan was silent or ambiguous on the points
below; each is the smallest sound choice and none changes a rule above. The probes of the 0701 addendum
still hold, and the probe this task owns ran first.

1. **The `--uart1-tcp` probe passed on the S3 model, so the production scenario exists.** A throwaway task on
   the production image (not committed) installed UART1 (`uart_driver_install`, 115 200 8N1, pins 38 and 39)
   and echoed, with a host peer on esp-emu 0.44.0's `--uart1-tcp` bridge (the emulator listens, the peer
   connects). The device's 64 bytes `00..3f` reached the peer intact; the peer's 1 000, 3 000 and 20 000 bytes
   came back identical, none lost; the first 256 bytes of a reply arrived 174 µs of emulated time after the
   device sent. The emulated UART has **no baud timing**: 20 000 bytes returned in 0.08 s of wall time, where
   115 200 baud needs 1.7 s each way, so bytes arrive at once whatever baud is configured, and the pins are not
   part of the bridge. `v07-uart-prod` therefore shows the real ESP-IDF driver, the real transport and the
   service working end to end; it never shows a baud rate or a level (every row is in docs/VALIDATION.md §2).
   The harness gains `uart1_peer = "echo"` (a thread in `scripts/test_emu.py`: it connects to the port the run
   opened, sends back every byte and nothing of its own, and fails the scenario if it cannot connect; the test
   image refuses the key, its UART is the model's) and three pokes.
2. **Three pure files and one session, no sixth substitution.** `lab_uart.c` (the baud and format lists, the
   byte ring, the loopback pattern and its judgement, the send file's grammar, log names, CRC-32, hex and ASCII
   rows), `lab_autobaud.c` and `lab_uart_svc.c` (the session over an injected I/O table: which pins it holds, the
   ring and counters, send, loopback, auto-baud) are pure and run at 100 % lines; `stark_lab.c` binds the session to
   the pin hold, the transport, two locks (one operation at a time; a short one around the ring and counters, so the
   UI can read them while the worker waits on the line), the log and the event bus. The UART group goes through
   the same `stark_lab_transport` as everything else: still five substitutions. The text helpers the app needs
   are the `stark_lab_uart_*()` functions of `stark_lab.h`, so the app includes one header.
3. **The monitor drives nothing; TX is armed like any lab output.** The plan's `uart_open(baud, fmt, rx_buf)` gains
   a `tx` flag: a session opens receive-only (the TX pad is never touched), and `stark_lab_uart_tx_enable()`,
   which the app calls only after the confirmation naming `LAB_UART_TX`, arms the pin (the arm of §3, per pin,
   cleared by release) and installs the driver again with TX routed at the weakest drive strength
   (`stark_hal_gpio_drive_weak()`). Send and the loopback test are `STARK_ERR_STATE` without it. The pad is not
   read back (a line a peripheral drives has no one level, like the PWM's, 0702 addendum 7): the confirmation
   is the guard. The confirmation is asked once per view (Send, Loopback test): the session, and with it the arm,
   lives from entering the view to leaving it, and the pins are held only while a view or a job needs them.
4. **The driver has an event queue, not none.** The plan installed it with no queue "so no driver task"; the
   driver has no task either way (it is interrupt driven), and without a queue ESP-IDF reports a framing error, a
   parity error or an overrun nowhere, which the spec requires counted and shown. The transport therefore
   installs a 32-event queue and every read drains it into the caller's counters; a storm faster than the
   reads (11 520 framing errors a second at 115 200 baud against a read every 10 ms) counts a lower bound,
   never zero. `set_baud` is in the API as specified; a change of baud alone uses it, a change of format installs
   the driver again (so TX, if routed, is released and routed again once; on the production image ESP-IDF
   logs `GPIO 38 is not usable, maybe used by others` there, its own reservation of the previous install; a
   glitch on a TX line that is wired is a V1 check).
5. **Buffers.** Driver 2 kB, ring 4 kB, edge buffer of one auto-baud 2 kB (512 stamps), log buffer 2 kB, a poll
   takes at most 64 bytes, a send 32, a write to the driver 64, the file 1 kB, the log 1 MiB: all heap, held from
   open to close (the log's from start to stop). Events `STARK_LAB_EVT_UART_DATA` (4) and `_UART_ERROR` (5) are
   published at most ten times a second each; 3 stays the reserved `_SCAN_DONE`, 6 is `_SPI_DONE`.
6. **The loopback test.** It empties the receive buffer (at most eight reads of 64), sends the pattern as two
   writes of 32 (each waits for its last character to leave: at 1 200 baud 0.27 s), and listens in slices of at most
   10 ms for twice the wire time of 64 characters plus 20 ms, never longer even if the clock stops. The judgement has
   one more outcome than the plan's: `ok` (every byte back, no error counted), `mismatch first <n>` (a byte differs or
   is missing, the offset of the first), `errors <n>` (the bytes match but the receiver counted framing, parity or
   overrun errors) and `no data`. The log line is `lab: uart loop <received>/<sent> <outcome>`: a slow peer that
   answers after the window gives `36/64 mismatch first 0`, not a success. BACK cancels it (a flag the loop reads, no
   lock); a transport failure ends the session.
7. **Auto-baud refines the shortest pulse, which alone cannot meet the plan's own vectors.** With edge jitter of
   ±1 µs the shortest of many 1-bit pulses is shorter than the bit by up to 2 µs, 23 % of a bit at 115 200: snapping
   that to the list says `unknown`. The rule is the plan's (pulses under 1 µs are glitches; the shortest of the rest
   is the one-bit pulse; the nearest standard bit time within 35 % is accepted; the result snaps to the list within
   3 %, else `unknown`) with one step added: the bit time is refined, twice, as the total time of all pulses up to
   twelve bits long over the total number of bits they hold (each rounded to the nearest whole number of the current
   estimate), which averages jitter out. A line with fewer than 16 edges, no pulse over 1 µs or a bit time near no
   listed baud is a status, never a number. The capture is 512 stamps from the RX pad for up to 2 s (it ends as soon
   as the buffer is full), through `pad_edges()` with the pin held, the pad an input, the pin released before
   anything opens the driver on it. The vectors' jitter is ±1 µs or a tenth of a bit, whichever is less; the
   real capture's jitter and the highest baud the interrupt can follow (the bit at 921 600 is 1.09 µs) are V1
   measurements.
8. **The send file is `uart_send.txt` in the card's `apps` directory.** The card has four directories and no
   subdirectories (ADR-0018), so the plan's `uart/send.txt` cannot exist. Its grammar, strict and whole: a file
   starting with `hex:` is pairs of hex digits separated by white space; any other file is printable ASCII
   (0x20 to 0x7e) with one trailing LF or CRLF allowed, so `AT\r\n` is written `hex:41 54 0d 0a`; at most 1 kB in and
   32 bytes out; anything else is rejected whole and nothing is sent. It is read and sent on the app worker.
9. **The card log** is `logs/uart_NNNN.log` (`STARK_DIR_LOGS`, the next free four-digit number, so 9 999 files at
   most), written on the worker through a 2 kB buffer: flushed when it fills and after a byte has waited a
   second, synced every 4 kB (a close and an append reopen, since the storage service syncs on close and has no
   other sync) and closed at 1 MiB, when it says `log full` and the monitor goes on. The card is never held open
   past the log's end, so it can be ejected. A card that goes away ends the log (`Log stopped`, the buffer is
   lost, the monitor goes on). The CRC-32 the log reports at its close, `uartapp: log closed uart_0001.log 1300 bytes
   crc 0x...`, is over the bytes it handed to the storage service; the storage service's own tests are what show
   they reached the card, and nothing reads the file back. The console log carries names and counts only.
10. **The app.** Home: Monitor, Send, Loopback test, Auto-baud, Log (on or off, used by the next monitor), Baud,
    Format (LEFT and RIGHT, stopping at the ends of the lists). The monitor is a screen of its own pushed over the
    menu (so BACK pops it and its `on_exit` closes the session, and a long BACK from anywhere closes it too): a
    header, the counters (`rx 1234  err 0  lost 0`, red when anything was lost or counted), the log's state and 17
    rows of the ring in the 6 x 10 font, 16 bytes of hex or 48 characters of ASCII, rows aligned to the newest
    byte; OK toggles, UP and DOWN scroll a row, LEFT and RIGHT a page. The Send view is the canned sends
    (`\r\n`, `AT\r\n`, `?\r\n`, Ctrl-C, Ctrl-D), the file, and a byte editor of rows as the I2C app's. The loopback
    test is a view of its own (Run test, Baud, Format, the line errors) so the settings are next to the button.
    Auto-baud is a home action that sets the Baud row.
11. **The test image's peer** (`fake_lab.c`, pokes `emu_lab_uart`, `emu_lab_uart_baud`, `emu_lab_uart_fmt`): 0 silent,
    1 TX wired to RX, 2 an echo peer at its own baud and format (a baud more than 3 % off or another format garbles
    each byte through a fixed bijection that never returns to the original and counts a framing error, or a parity
    error where the receiver checks parity), 3 a peer that talks (the text scene of 50 lines, 1 300 bytes, once from
    the moment the driver opens) and 4 the binary scene (1 024 bytes; the plan's three modes plus one). The same
    peer is what auto-baud sees on the RX pad: the scene again and again from the start of a capture, edge by edge at the
    peer's baud, with the jitter the edge capture is given. Characters that have not crossed the wire by "now" have not
    arrived, so the loopback window is exercised for real; the host shim and the test image wait in model time.
    `storage/v07-uart` (loop, mismatch at 9 600 against 115 200, auto-baud at 38 400, monitor, send),
    `v07-uart-bauds` (every baud), `v07-uart-formats` (every format) and `v07-uart-log` (the scene logged, its
    CRC-32, the pins free for the GPIO app) each stay inside 20 s of emulated time.
12. **Evidence.** *Host:* `test_lab_uart`, `test_lab_autobaud` (72 edge sets: every baud, format, pattern, jitter, gap,
    spike, the 3 % boundary from both sides, hostile input), `test_lab_uart_svc` (the session over the real pin hold
    and the shim: every baud and format in the loopback and in auto-baud, every failure), `test_uart_views`,
    `test_uart_log` (1 MiB exactly, a card that goes away, every card error), `test_ui_uart` (37 frames pinned after
    inspection) and an independent Python reference (`scripts/gen_lab_vectors.py`, `fake_lab_uart_vectors.h`) for
    the wire times, the ring, the loopback judgement, the rows, the file grammar, the names, zlib's CRC-32 and the
    estimator. `lab_uart.c`, `lab_autobaud.c`, `lab_uart_svc.c`, `uart_views.c`, `uart_log.c` and `fake_lab.c` run at
    100 % lines. *Production image:* `v07-uart-prod` (the monitor opens receive-only; the loopback pattern comes
    back through the real driver and a host peer at 115 200, 230 400 and 230 400 8E1; both pins released). *Test image:*
    the four scenarios above. Nothing here shows a baud on a wire, a level on a pin or a real adapter answering
    (ROADMAP V1 exit 13).
13. **Cost.** The binary grows by 38.2 kB, 14.1 kB of it ESP-IDF's UART driver (`libesp_driver_uart.a`, which no
    earlier task linked), against a per-task flash budget of 12 kB that did not count it. The 52 kB ceiling of §10
    for the whole of V0.7 is passed (the binary is 549 kB; the partition is 4 MB, 87 % free): this task flags it and
    does not move it, and STARK-0708 restates the ceiling from what V0.7 actually costs. The boot-heap cost is in
    docs/measurements.md; the V0.7 total stays inside the 16 kB.

**Addendum (2026-11-24, STARK-0706).** The SPI probe. The plan was silent or ambiguous on the points below; each is the
smallest sound choice and none changes a rule above. The one decision that matters stays §6's: **the probe runs only on a
slot whose module declares `MOD_SPI` and no function bit**, so no path leads from a raw transfer to a driver's chip.

1. **The eligibility rule is one inline function, judged four times and tested against a fifth statement.** *A slot reads
   `KNOWN`, its descriptor declares `MOD_SPI`, and nothing else is declared except `MOD_IRQ`.* It is
   `stark_lab_spi_entry_ok()` in `stark_lab_transport.h`. The service judges it at the request, at the send before it claims,
   and again at the send after the claim (a module cannot change under a lease, since a replacement revokes it, so the second
   look closes the gap between the first look and the claim); the transport judges it a fourth time, on the live table,
   before it attaches anything, whatever the caller says. `lab_spi_judge()` adds the *reason* around the same predicate, and
   the independent Python table in `scripts/gen_lab_vectors.py` (220 vectors: every state, 22 declared-bit mixes, the chip
   select wired or not) gives the same answer for every one. An unknown bit is a function (`owned by unknown`): a future module
   that declares something this firmware has no driver for is not a breakout. Reasons are decided in this order: `no chip
   select` (a fact about the board, so slot 1 always says it), `no module`, `generic module` or `unsupported module`,
   `conflict`, `owned by Sub-GHz` / `NFC` / `IR` / `unknown`, `no SPI declared`.
2. **The request is the arm.** The plan's API has `request` and `send` and no `arm`, so `stark_lab_spi_request()` is what 0704
   called `arm`: it validates, judges, records a fingerprint (32-bit FNV-1a over `'S'`, slot, clock, mode, count, the bytes and
   the slot's module identity: id, revision, declared bits) and replaces any earlier request, and `stark_lab_spi_send()` spends
   it. The fingerprint is **recomputed from the request the caller hands back and from the table as it is then**, so a changed
   byte, clock, mode or count, a different module in the slot, or a request that is not the armed one is `STARK_ERR_STATE`
   (`lab: spi send refused: changed`). The request struct carries no fingerprint of its own for that reason: nothing a caller
   writes there could be believed. As in 0704, any send spends the arm, success or failure (it is cleared before the arguments
   are even looked at, so "consumed before the bus is touched" holds for a transaction that then fails), a bad request leaves no
   arm behind, and an arm is stale after 30 s with the clock injected and its wrap handled. *The module's identity is a
   descriptor, not a serial number:* a module replaced by one with the same id, revision and bits in the 30 s between the
   dialog and the OK is the same transaction as far as this check can tell, and that is no weaker than the dialog, which names
   the slot, not a part. The lease generation the plan mentions is not observable (`stark_module_release()` bumps it, so a
   claim-and-release at the request would learn nothing), so the identity stands in for it; the live lease still goes stale the
   moment the module goes (`stark_module_spi_xfer()` returns `STARK_ERR_STATE`, tested).
3. **The claim is made at the send and never held.** `request` claims nothing (so a dialog can sit open while a driver works),
   and `send` claims `MOD_SPI` only, as `spiprobe`, after the first look and releases after the transfer in every outcome. A
   busy slot is `lab: spi slot 0 busy` and `STARK_ERR_BUSY`, with the driver's lease untouched; any other claim error is
   passed on with its text. `STARK_LAB_EVT_SPI_DONE` is published when a transfer was attempted, successful or not, never for
   a refusal. The probe has a mutex of its own (a transaction never holds up a pin, a sweep or a UART read) and runs on the
   caller's task, the UI task: 32 bytes at 100 kHz are 2.6 ms.
4. **The transport call is as specified and attaches for itself.** `stark_lab_transport_spi_xfer(lease, hz, mode, tx, rx, n)`
   checks its arguments (1 to 32 bytes, one of the six clocks, mode 0 to 3), the entry rule on `stark_module_list()`, and then
   `stark_module_spi_attach()`, `_xfer()` and `_detach()` on the lease: no device handle outlives a call, and `stark_module_spi.c`
   remains the only file that calls the SPI driver (`check_layers.py` and `scripts/check.sh` are unchanged). There is **no call
   that names a pin or a device**: the chip select is the lease's, so the panel's and the card's devices on the shared bus cannot
   be addressed from the probe, which is the answer to "the display and SD share SPI2". The host shim and the test image's
   transport apply the same header-level guard in front of the device model (the fifth substitution carries the SPI group; there
   is no sixth).
5. **Catalogue entry 34 is the SPI breakout; entries 2 and 3 were already eligible.** The new entry (`scripts/gen_module_vectors.py`)
   is id `0x0B01`, revision 1, caps `0x00000008`, name `SPI breakout`, and nothing before it moved. Entry 3 (`spi_only`, `0x08`) and
   entry 2 (`spi_irq`, `0x18`) were in the catalogue from V0.5 and are probe-eligible too: the host tests use all three. Descriptor
   and rule are in `docs/module-format.md`.
6. **The fake devices are spec-derived and stateless.** `fake_lab.c` gains `fake_lab_spi_device()`: `NONE` (MISO idles low, 0x00),
   `FLASH` (`9f` returns `ef 40 18` after a zero byte, `05` returns status 0, `03` plus three address bytes streams the counting
   pattern, every other command is accepted and ignored: no write, no erase, no state) and `ECHO` (MISO is MOSI one byte late),
   and two faults, appended to `fake_lab_fault_t`: `SPI_ERROR` (the transfer fails) and `SPI_NO_ANSWER` (MISO reads 0xFF). The
   test image's pokes are `emu_lab_spi` (0 none, 1 flash, 2 echo) and the existing `emu_lab_fault` (4 and 5); an external
   chip can be bound to the model (`fake_lab_spi_ext()`), which is how the host test puts the fake CC1101 behind slot 0 and shows
   that nothing reaches it. Answers are checked against an independent Python model of the same devices (`fake_lab_spi_vectors.h`).
7. **The app follows the module table and acts on what is true now.** Home lists both slots (the module's name when it can be
   probed, else the reason) and redraws on `STARK_SYS_MODULE`, which adds the one launcher-style edge `apps/app_spi -> stark_module`
   that `app_modules` already has; OK on a slot re-reads the table first, so a list that went stale between redraws still never
   acts on a slot that has since become a radio's. A refused slot is an alert (`No probe`) and a log line, never a silent no-op.
   Setup is Clock, Mode, the three presets, Bytes, Nibble, the bytes, Send, a dimmed note, then the answer. A preset loads its
   bytes (the clock and mode stay) and moves the selection to Send; after a successful send the selection moves to the first
   MISO row, because the answer is what the user came for and the viewport follows the selection (the result rows are
   selectable and inert so they can be scrolled to: 32 bytes are four hex and four text rows). Editing a value, BACK and leaving
   the app cancel the request. The default is the JEDEC ID at 1 MHz in mode 0.
8. **Logs (contract).** Service: `lab: spi slot 0 1 MHz mode 0 tx 9f 00 00 00 rx 00 ef 40 18`, `lab: spi slot 0 1 MHz mode 0 tx
   9f 00 00 00 failed: <error>`, `lab: spi slot 0 busy` (or `<error>`), `lab: spi request refused: <reason>`, `lab: spi send
   refused: not confirmed | changed | <reason>`. App: `spiapp: home`, `open slot 0`, `slot 0 refused: <reason>`, `preset JEDEC
   ID`, `confirm slot 0 1 MHz mode 0`, `cancelled slot 0`, `sent slot 0: rx 00 ef 40 18`, `send slot 0 failed: <error>`, `request slot
   0 refused: <error>`. The module service adds `module: slot 0 claimed by spiprobe` for each transaction.
9. **Evidence.** *Host:* `test_lab_spi` (the 220 judgement vectors against the reference and the shared predicate; the 14
   fingerprint vectors; every clock, mode and length at its edges; a request consumed by success, by failure, by a bad argument and
   by a cancel; stale at 30 s and 30 s + 1 ms, across the clock's wrap; a changed field, a replaced request, a different module,
   a module swapped for a radio or pulled before the send and between the claim and the transfer; a stale lease; a busy slot; the
   claim released after every outcome; and **the radio sees no transfer**: with the CC1101 module in slot 0 and the fake chip bound
   behind the SPI model, a PATABLE burst, an `STX` strobe, 32 bytes of `ff` and a JEDEC read are refused at the request, at a
   hand-made send and at the transport with a lease the radio's own driver could hold, and the chip's transfer count, `PATABLE`
   writes, strobes and `MARCSTATE` do not move, while the same harness on an eligible slot does reach the chip, so the test cannot
   pass by reaching nothing), `test_fake_lab` (the devices, the faults, the binding), `test_spi_views` and `test_ui_spi` (13
   scenarios, 16 frames pinned after inspection), `test_module_desc` (entry 34). `lab_spi.c`, `spi_views.c` and `fake_lab.c` run at
   100 % lines. *Production image:* `v07-spi-prod` (slot 0 reads generic because esp-emu answers at 0x50, slot 1 has no chip select:
   both refused, no claim, no transfer). *Test image:* `storage/v07-spi` (entry 34 and a flash: cancel sends nothing, OK one
   transaction, a second press asks again, an echo, then the CC1101 module: `owned by Sub-GHz`, the driver's detection unchanged,
   never claimed by the probe, then an empty slot). Nothing here shows a signal on MOSI, a chip select moving, the panel and the
   card staying intact, or a real flash answering (ROADMAP V1 exit 13).
10. **Cost.** In docs/measurements.md "V0.7 lab tools": boot heap, binary, static RAM and flash against the 0706 budgets.

**Addendum (2026-11-24, STARK-0707).** The self-test and the report export. The plan was silent or ambiguous on the points
below; each is the smallest sound choice and none changes a rule above. The rule that matters stays §8's: **a check the device
cannot judge for itself is `attested` when the operator says yes, `fail` when the operator says no, and never `pass`**, and
the report never leaves the device.

1. **Where the code lives, and one split the plan did not make.** The runner (`selftest_core.c`) and the report formatter
   (`selftest_report.c`) are pure files of `stark_diag` over injected pieces (a clock, a lock, a notifier, a sink), at 100 %
   lines. The `stark_selftest_*()` calls are `selftest_svc.c`, one instance of the runner with the log lines and the events;
   it needs only the log and the event bus, both of which the host has, so the host UI port runs the very same file and
   `stark_diag.c` only hands it the clock (`esp_timer`) and a mutex. The ten checks and the export are the composition root's
   (`main/selftest_checks.c`, `selftest_export.c`), **pure over one table of calls** (`main/selftest_env.h`: heap, flash read,
   settings, card, module list and probe, diagnostics, the log tail), so the host tests give them a model of the machine they
   can break one call at a time, and `main/selftest_target.c` (not in the plan's file list) fills the same table from the real
   services and registers the checks. `stark_diag` gains no edge; the root requires `stark_hal`, `stark_module_transport`,
   `app_update` and `spi_flash`, which the root may. `main/selftest_sha256.c` is a plain FIPS 180-4 SHA-256 over reads (item 5).
2. **API additions to the plan's list.** `stark_selftest_set_export()` and `stark_selftest_export(name, cap, &bytes)` (the
   app cannot call the root, so the root installs the exporter; the size is for the log), `stark_settings_source()` (item 6),
   `stark_log_ring_tail()` (item 12), and the check indices the app relies on (`STAPP_BUTTONS`.. in `selftestapp.h`, which
   `test_selftest_checks` compares with the root's enum). Not added: a public summary call. The summary line the log carries is
   the core's (`selftest: summary 7 pass  3 attested  0 fail  0 skip`); the screen builds its own from the rows, in a compact
   form (`6 pass  0 att  1 fail  1 skip  2 left`) while some are not run, because the full text does not fit a screen row.
3. **The runner's contract.** A check with `run == NULL` is interactive: the app observes it and records the result with
   `stark_selftest_report()`; running it is `STARK_ERR_NOT_SUPPORTED`. Every check has a deadline of 10 s judged **when it
   returns** (a check cannot be preempted, so every check keeps its own loops bounded, and a check that took longer is
   `fail`, `timeout`, whatever it returned). A value outside pass, fail and skip is `fail`, `bad result`. Evidence is cut at 48
   characters and loses its control characters. A run that never returned (the contained app faulted inside a check) leaves
   its row `not run`, so the table keeps what finished, and the next run records it `fail`, `app_fault`. **Clearing a row
   (`ST_NOT_RUN`) is silent**: no log line, no event, so a second Run all cannot flood the bus (queue 32) or print a summary.
   The log line is `selftest: <name lower-cased> <result> <evidence>` (`selftest: ram pass 32768 B`, `selftest: status led
   attested operator: blinks once a second`); `STARK_SYS_SELFTEST` carries `_STEP` after each result and `_DONE` when no row is
   left `not run`.
4. **RAM.** One block of the smaller of 32 kB and a quarter of the largest free block, **and never so much that free heap
   would fall below 128 kB** (so on a heap near the floor the block shrinks, under 1 kB it is `fail`, `heap too low`, and nothing
   is allocated), a whole number of words, from the internal 8-bit heap only. After the allocation the free heap is read again
   and a fall below the floor (another task took memory) is a failure before any pattern is written. Twelve byte patterns (walking
   ones, 0x00, 0xFF, 0xAA, 0x55) and the address-in-address word pattern, each written, given a dwell and verified through
   `volatile` pointers; the first mismatch is named (`bad byte +100 read 11 want 01`). **The dwell is a hook in the environment
   table (`ram_dwell`) that the firmware leaves NULL**: it is the one place the host test flips a bit, and the only test-only seam
   in the production check. The block is freed in every outcome.
5. **Flash, read-only, and what "the image header's size" means.** The chip size from `esp_flash_get_size()` must equal the
   size nibble of the bootloader image header at flash offset 0 (byte 3, upper half: the flash size the image was built for;
   sizes 1 to 32 MB are known). Then two SHA-256 passes over **the running image**, not the whole partition: the length is found by
   walking the app image header, its segment headers, the padding to the checksum byte and the appended digest if the header
   says so (`selftest_image_length()`, tested over 16 segment counts and every malformed header), because hashing the 3 MB
   partition twice would take seconds for nothing. Reads are 256 bytes at a time through `esp_flash_read()`; nothing is erased or
   written, which the host model enforces by having no call to do it and `scripts/check.sh` (`selftest-flash-readonly`) enforces
   on the target by refusing any flash, partition, OTA or NVS write or erase call in a self-test file. The appended digest is not
   compared (the plan asks for two passes that agree); a check against it is a V1 candidate. **The hash is plain C**, not mbedTLS
   (nothing else links it) and not the SHA accelerator (the emulator does not model it): 1 210 B of code, FIPS vectors and every
   block boundary tested.
6. **Settings: read, never write.** All four settings are read twice and must agree and lie inside their descriptor's range and
   step, and `stark_settings_source()` (new; NVS, defaults, or RAM only) must not be RAM only: a store that could not be opened is
   `fail`, `store unavailable, RAM only`. The plan's "a read-back of one value" would not have noticed an unreadable NVS.
7. **Storage: skip, fail or pass by the state.** No card, no slot or an ejected card are not faults: `skip`, with the state
   as evidence (`selftest: storage skip no card`). A card that was mounted and went away (`REMOVED`) or answers and holds no
   volume (`UNUSABLE`) is what the status bar shows as an error, so it is `fail` with the state. A mounted card gets one atomic
   file `logs/selftest.chk` of 4 096 bytes (16 chunks of a counting pattern), closed, reopened, compared and removed; each step
   has its own evidence (`write failed: I/O error`, `short read at 3996`, `read-back differs in block 1`), a failed write is
   discarded and a file that closed is removed even when the comparison failed.
8. **Module bus: the table against the wire, both ways, with one retry.** For each slot with an ID EEPROM address the table's
   state is compared with an address probe through `stark_module_transport_probe()` (5 ms): a module that is present but does not
   ACK, and one that is absent but ACKs, both fail (`slot 0 present, 0x50 no ACK`). The table is a second old, so a disagreement
   is looked at once more; **a bus fault (any probe error but a NACK) is the finding and is not retried**; no bus is `skip`.
9. **Events and stacks.** Event drops 0 and every task (at most 16) with at least 256 B of stack free in the `stark_diag`
   snapshot. The emulator's production image reports 12 tasks with at least 568 B, so the margin is real.
10. **The walk, and what BACK means.** Run all runs the six automated checks on the app worker and, when they are done, pushes the
    walk screen: **Buttons** asks for the six keys; **Display** shows the colour bars and the grey ramp; **Buzzer** plays three
    rising tones (UP plays them again); **Status LED** asks about the heartbeat. The plan's "BACK abandons the walk" and "BACK on
    one records `fail`" are one rule here: BACK on an attested question records `fail` **and ends the walk**; the rest stay
    `not run`. In the Buttons step BACK is the sixth key, observed and consumed, and does not leave the app. Buttons: a key counts
    once on its **press** in any order (the prompt names the next one, the plan's "in turn"), the limit is **30 s for the whole
    step** (the plan says "within 30 s" without a unit), read from the once-a-second diagnostics sample, and when it runs out the
    step is `fail` naming the missing keys (`missing DOWN,LEFT`) and the walk goes on to the questions. **An answer needs its
    press to have arrived in its step**: the release and short press of the key that finished the previous step (BACK, say) arrive
    in the next one and must not answer it (tested). Leaving the app, by a long BACK or the manager, stops the tones; what
    finished is kept. BACK during Run all asks the worker to stop, and it stops after the check in progress (a check cannot be
    preempted): the join waits up to 1 s (`CONFIG_STARK_APP_WORKER_JOIN_MS`), and the longest check, the flash one, is about
    0.3 s by arithmetic on silicon (4 500 reads of 256 B and 2.3 MB hashed): a V1 reading, not a measurement made here.
11. **The report: bounded, one line per fact, whole or not at all.** `logs/report_NNNN.txt`, the number one past the highest
    present (so `report_0001.txt` first, never replacing one, `STARK_ERR_NO_SPACE` after 9 999), written through a cursor formatter
    one line at a time into an atomic card file: firmware, build, board, uptime, the snapshot line, the tasks, the module-table
    lines, the storage state, every self-test row with its result, `(operator)` for an attested one, and its evidence, the summary
    and the log tail. **Every character is printable or a newline**: in every field, a control character (a newline too) becomes
    `.` so a hostile module name or log line cannot start a new line or an escape sequence; in the tail only the newlines are kept.
    The cap is 16 384 bytes with 16 bytes reserved for `-- truncated --`, and the tail is at most 4 096 bytes of whole lines. The
    transient 5.5 kB (snapshot, tasks, module lines, rows, the tail window) is heap, held for the export and freed in every
    outcome. **Beyond the plan, the file is read back and its length and CRC-32 must equal what was written** before the app says
    it is saved; otherwise it is removed and the export is `STARK_ERR_IO`. No error leaves a file behind (tested at every write).
12. **The log tail is non-destructive.** `stark_log_ring_tail()` (new, `log_ring_tail()` in the pure ring, 100 % lines) copies the
    newest whole lines without consuming them, because the storage service drains the same ring into `logs/stark.log`: the report
    sees the lines since that service's last drain (about a second), not the whole file. That is what the plan's "the tail of the
    log ring" can mean. Nothing else is collected: no setting value, no file content, no credential.
13. **No card.** The app says so (`No card`, an alert) and logs `selftest: export skipped (no card)`. The plan's "the report is
    shown" is read as: the results are on the screen already; the report text is not rendered. Any other error is `selftest: export
    failed: <error>`; success is `selftest: export report_0001.txt 1447 B` and an alert naming the file.
14. **Launcher and scenarios.** Self-Test is System row 3 (after Diagnostics), the registry line is `n=18` with `selftest` after
    `diagnostics`, and every later row moves down by one. Edits to pre-existing scenarios are the registry line, launcher row
    numbers (+1 from App Test down) and one more DOWN press where a walk from About crosses the new row; they are listed in
    TASKS.md STARK-0707. Two scenarios on the test image, not one: `v07-selftest` (a card: Storage passes, the export) and
    `v07-selftest-faults` (the card pulled and the module bus timing out), so each stays far inside 20 s.
15. **What the emulator cannot show, and the tests that do.** A card failing part-way through the 4 kB write cannot be timed by
    a poke (the check lasts a few milliseconds and the storage service notices a pulled card within 150 ms, so the emulator
    reports `removed`, not a failed write), so every step of that failure is host-only, as are the corrupted RAM word, the flash
    size and digest mismatches, NVS unreadable, a drop, a thin stack and a module that declares and does not ACK. The emulator's
    flash check passing shows the check's logic against the emulated flash (16 MB there, the image header agreeing), never the
    chip's.
16. **Evidence.** *Host:* `test_selftest_core`, `test_selftest_report`, `test_selftest_checks`, `test_selftest_export`,
    `test_selftest_views`, `test_ui_selftest` (16 frames pinned after inspection) and three more tests in `test_log_ring`;
    `selftest_core.c`, `selftest_report.c`, `selftest_svc.c`, `selftest_checks.c`, `selftest_export.c`, `selftest_sha256.c`,
    `selftest_views.c` and `log_ring.c` at 100 % lines. *Production image:* `v07-selftest-prod`: the six automated checks (storage
    skipped: no card is modelled), the injected keys, three attestations, `6 pass  3 attested  0 fail  1 skip`, heap before and
    after equal. *Test image:* `storage/v07-selftest` (`7 pass  3 attested  0 fail  0 skip`, `report_0001.txt` and `report_0002.txt`
    written and read back) and `storage/v07-selftest-faults` (`storage fail removed`, `module bus fail probe 0x50 failed:
    Timeout`, the walk ended by BACK). Nothing here shows a screen, a tone or an LED being observed by a person, a real flash, or
    RAM on silicon (ROADMAP V1 exit 13).
17. **Cost.** In docs/measurements.md "V0.7 lab tools": boot heap, binary, static RAM and flash against the 0707 budgets. The flash
    budget of 8 kB was exceeded; the binary ceiling of §10 was already passed at STARK-0705 and STARK-0708 restates it.

**Addendum (2026-11-24, STARK-0708): V0.7 exit, the restated budget.** The exit task builds no firmware: it adds the soak, the
evidence tables, the V1 checklist, the ADR-0005 scoring and a faster CI, and it **restates the V0.7 budgets with what was
measured**. The owner delegated this decision and it is recorded here, not taken silently. Nothing below moves a safety rule,
and no gate was loosened: the boot-heap gate stays 200 kB and the V0.7 boot-heap budget stays 16 kB.

1. **What was planned and what was built.** The plan (§10, the TASKS.md V0.7 header) was a V0.7 binary ceiling of +52 kB over the
   V0.6 close (483 216 B, so at most 535 kB) and per-task budgets whose flash column adds up to 50 kB. Seven tasks reported against
   their own budgets as they landed; this table puts them side by side (1 kB is 1 024 B in the per-task columns, as the tasks
   measured it; the image column is `build/stark-one.bin`, and the seven deltas sum to exactly 88 800 B):

   | Task | Boot heap budget / cost | Static RAM budget / actual | Flash budget / actual (as the task recorded it) | Image growth | Verdict |
   | --- | --- | --- | --- | --- | --- |
   | 0701 pin safety, the seam | ≤ 1.5 kB / 432 B | ≤ 1.5 kB / 414 B | ≤ 3 kB / 1 477 B | +3 312 B | inside |
   | 0702 GPIO app | ≤ 1 kB / 1 004 B | `app_gpio` ≤ 2 kB / 786 B | `app_gpio` ≤ 7 kB / 4 378 B | +9 328 B | inside as the budget was worded; the image grew 2.1 kB more than 7 kB because `stark_lab`, the transport, the HAL and the IDF GPIO and LEDC drivers are not in the `app_gpio` row |
   | 0703 measurement | ≤ 0.5 kB / 352 B | ≤ 0.5 kB / +97 B | ≤ 4 kB / 3 074 B | +4 496 B | inside |
   | 0704 I²C scanner | ≤ 2 kB / 1 548 B | `app_i2c` ≤ 2 kB / 1 364 B; lab I²C ≤ 1 kB / +128 B | ≤ 10 kB / 8 482 B | +10 752 B | inside |
   | 0705 UART tool | ≤ 3 kB / 4 572 B | `app_uart` ≤ 2 kB / 1 388 B; lab UART ≤ 1.5 kB / +461 B | ≤ 12 kB / 15 523 B of our code, and 13 759 B of the ESP-IDF UART driver | +38 240 B | **over**: boot heap by 1.5 kB, flash by 3.2 kB of our code, and the driver, which no earlier task linked |
   | 0706 SPI probe | ≤ 1 kB / 364 B | `app_spi` ≤ 1 kB / 170 B | ≤ 6 kB / 6 470 B | +7 440 B | **over** by 326 B of flash (the reason texts, log lines and the 49-row answer view) |
   | 0707 self-test | ≤ 2.5 kB / 1 340 B | ≤ 2.5 kB / about 1.3 kB | ≤ 8 kB / 15.2 kB | +15 232 B | **over** by 7.2 kB of flash (the checks' evidence texts, the image walk, the SHA-256 and the report formatter; the budget did not count the walk screen or the export's read-back) |
   | **V0.7** | **≤ 16 kB / 8 780 B** (231 952 B; gate ≥ 200 kB) | — / +6 368 B (`.bss` and `.data`, 80 495 to 86 863 B) | ≤ 50 kB across the tasks / 54.6 kB of our own code | **≤ +52 kB / +88 800 B** (572 016 B) | **ceiling passed at 0705**; boot heap inside |

2. **Where the 88 800 B went**, measured by component against the V0.6 close (`idf.py size-components` on a build of the V0.6
   commit taken from a `git archive`, and on this tree; docs/measurements.md "V0.7 exit criteria"). Our new code is about 55.7 kB of
   flash (`stark_lab` 17 061, `app_uart` 8 224, `main` +5 522 for the self-test checks and the export, `app_i2c` 5 012, `app_gpio`
   4 963, `app_selftest` 3 937, `app_spi` 3 870, `stark_diag` +2 747, `stark_lab_transport` 2 571, `stark_hal` +1 133,
   `stark_board` +463, `stark_log` +210). The ESP-IDF code the lab tools link is about 19 kB of archive total (`esp_driver_uart`
   +10 970, `esp_ringbuf` +5 099 and `esp_hal_uart` +1 068 for the UART driver, `esp_driver_gpio` +1 465 and `esp_driver_ledc` +386 for
   the pad and PWM calls). The `libesp_stdio.a` row of the same table grows by 12 771 B between the two builds although its object
   list is unchanged (`stdio_vfs.c.obj` only), so that row is the tool's attribution of linked libc sections and is **not**
   claimed as the cost of any V0.7 task. Everything else moved by less than 100 B each.
3. **The restated ceiling: +90 kB over the V0.6 close, at most 573 216 B** (the plan's +52 kB, 535 216 B, no longer holds). It is the
   measured +88.8 kB rounded up to the next kilobyte, not a target to grow into: the closing task adds no firmware, and every later
   milestone carries its own ceiling in its own plan. Why it is acceptable: the image is 13.6 % of the 4 MB factory partition
   (86 % free); the growth is the lab tools themselves plus the ESP-IDF UART driver that the plan assumed would be a thin wrapper
   (§2 said "the UART driver is installed without an event queue" and sized nothing); and no flash went to anything that is not a
   V0.7 feature. The two ways to take some back (a UART driver written on the LL layer for about 14 kB, and sharing the lab apps'
   reason texts) are real work with real risk to safety behaviour that the host and emulator tests now pin, so they are options for
   a later size task and are not done at an exit. **Rejected:** moving the ceiling silently; shrinking the self-test or the UART
   tool to fit (the exit criteria are met by them); dropping the UART driver's ring buffer (it is the driver's, not ours).
4. **Unchanged, and reported against: the boot-heap gate and the 16 kB budget.** Production boot heap is 231 952 B against the
   240 732 B of the V0.6 close: **8 780 B of the 16 kB spent (55 %), and 31 952 B above the 200 kB gate**. The V0.7 closing figure is
   therefore ≥ 224 kB as §10 required. No task cost more than 4 572 B (0705, which exceeded its 3 kB budget), and the per-task heap
   budgets add up to 11.5 kB, inside the 16 kB. The test image's boot heap is 225 032 B (V0.6: 237 812 B). Static RAM grew by 6 368 B,
   which is where the boot heap went. Transient buffers (the 4 kB edge ring, the UART driver with its 4 kB ring and 2 kB log buffer,
   the RAM check's 32 kB block, the report) are heap held only while a view runs, and the root heap after leaving every app is within
   1 kB of before, as the soak measures.
5. **The one rule that changes.** No per-task budget is re-opened: a task's budget is a plan, and 0705 to 0707 reported their
   overruns when they landed. From V0.8, **a flash budget in a task spec is read against the image growth as well as the
   component's own code, and names the ESP-IDF drivers the task will newly link**, so that a ceiling cannot be passed by a
   dependency nobody counted.
6. **The soak is longer than the plan's 60 s.** The plan said "60 s, two passes of about 30 s". The full sequence (Self-Test with a
   walk and an export; GPIO through a confirmed drive, a contention, PWM, a pulse, a measurement and a hot-plug withdrawal; the I²C
   scan with a confirmed and a refused write; the SPI probe and its refusal; the UART loopback and a log) takes about 60 s a pass at
   the key spacing the existing scenarios proved (500 ms around a dialog, 150 to 200 ms between navigation keys). Shortening it
   would have dropped one of the tools from the soak or run it at spacings that do not render reliably, so each pass is about 60 s
   and the soak ends at 122.5 s of emulated time. The gates are unchanged (`drops=0` on every `stark_diag` line; the first and the
   last root heap within 1 kB, the last line taken at the launcher 121 s in) and the wall-clock cap is 3 600 s (V0.6's soak: 1 500 s
   for 62 s). Plan against actual is recorded here; no other soak changed.
7. **CI: the test-image scenarios run in four shards, the soaks as a matrix.** The `esp-emulator-storage` job ran the 39 scenarios of
   the test image one after another and took about an hour. It is now `emu-storage-image` (builds the image once, runs
   `check_emu_image.py` and the shard check, uploads the merged binary and the ELF), `esp-emulator-storage-shard` (a matrix of four,
   each downloading that image and running its share) and a final job still named `esp-emulator-storage` that succeeds only when
   the image job and every shard did, so the check name does not change. The partition is `scripts/emu_shards.py`: scenarios
   ordered by weight (emulated duration plus 20 s of boot) then name, each given to the currently lightest shard, ties to the lowest
   index. It is deterministic and independent of directory order, and every scenario of `test/emu/storage/*.toml` lands in exactly
   one shard (`--check`, run by the image job, and by `scripts/test_emu_shards.py` in the lint job for every shard count from 1 to
   8, with negative tests for a dropped, a doubled and a foreign scenario; one more test ties the matrix in `ci.yml` to the shard
   count that is tested). Four shards because the longest scenario is 54 s of emulated time (`v06-detect`) and the weights balance
   to within 14 s (9 to 10 scenarios each, 337 to 351 s of weight). Nothing was dropped or weakened: no scenario changed,
   `check_emu_image.py` still runs (once, on the image every shard then downloads) and the harness still refuses a scenario on the
   wrong image. The soaks are `esp-emulator-soak-run`, a matrix with one entry per soak (V0.1 on the production image, V0.2 to V0.7 on
   the test image), behind a final `esp-emulator-soak` check. On the step name: the step that read "only the three allowed
   substitutions" was already "five" in this tree (STARK-0701 changed it); the only remaining "three" is the history text of
   TASKS.md STARK-0502 ("allows exactly three substitutions"), which says what that task allowed at the time and is left as written.
8. **The plan's V1 check "PWM on `LAB_UART_TX` wired into `LAB_UART_RX`" is restated** (the 0703 addendum left it to this task). It
   needs two live pins at once, which the one-pin GPIO app cannot give, and there is no second tool. In
   `docs/bringup/v1-labtools.md` check 17 closes the same loop with two independent references: the device's own PWM at 1 kHz,
   10 kHz and 40 kHz on a logic analyser (check 12), and a signal generator at the same frequencies and duties read by the device
   (checks 15 and 16); the two columns must agree within the sum of the tolerances. The UART loopback is not a substitute (it shows
   bytes, not an edge measurement). A lab header, when the V1 PCB has one, could wire a pin to a pin; the check does not depend on it.
9. **ADR-0005 is scored** in its own addendum: one trigger of four (the UART monitor's scrollable text, counted conservatively), so
   the in-house UI stays, and what would change the answer is written down.
10. **Readings the tasks recorded, in one place** (the earlier addenda and docs/VALIDATION.md §2 hold the detail): on esp-emu 0.44.0
    an output pad reads back and a self-driven pad raises its interrupt, the I²C sweep finds exactly 0x50, its EEPROM accepts writes (so
    the write refusal is the firmware's alone), pull-down reads and LEDC on a pad do not work, and the UART1 bridge round-trips through
    the production driver with no baud timing. Hence the production-image scenarios show the driver and the service, the test image
    shows the rules over a spec-derived model, the host shows the arithmetic against independent Python references, and **nothing
    before V1 shows a level on a pad, a waveform, a real baud, a real device or a person watching the panel**
    (`docs/bringup/v1-labtools.md`, 36 blank checks, ROADMAP V1 exit 13).
11. **Choices made where the plan was silent, each the smallest sound one:** the soak switches the PN532 off (`emu_nfc_fault = 1`),
    so the I²C scan lists the known devices and the module's EEPROM and NFC is dimmed and skipped in the launcher walks (every walk's
    row count was read from the `menu: sel=` lines of a real run, and the soak's `expect` list pins them); the module sequence is
    slot empty (pins 40 to 42 offered) → the breakout appears while pin 40 is driven (withdrawal) → the I²C refusal and the SPI probe
    on it → swapped for the CC1101 module (the SPI refusal, `owned by Sub-GHz`) → pulled during the UART section; the SPI `no module`
    refusal, the I²C dump, the UART bauds and formats, auto-baud and the format-change glitch stay in their own scenarios (the soak
    is about leaks and drops, not coverage); the second pass uses the same keys as the first; the report number advances
    (`report_0001.txt`, `report_0002.txt`; `uart_0001.log`, `uart_0002.log`); and the LVGL trigger counts the monitor's scrolling as
    met, the conservative reading, which still leaves the score below two.

**Revisit trigger (V0.7 exit):** the V1 lab header or a second UART (the GPIO app could then hold two pins and the plan's PWM loop
could be wired), a size task if the image approaches the partition (the UART driver of point 3), or the first V1 reading of drive
strength, ISR latency or auto-baud.

---

## ADR-0024 — V1 prototype parts: one list, prototype circuit values, 3.3 V only

**Status:** accepted (2026-11-26). The owner delegated product decisions and asked for
autonomous progress; recorded here rather than taken silently. STARK-0802 writes
`docs/bringup/v1-parts.md`.

**Context.** V0 to V0.7 are complete in software and every physical gate is V1 (ROADMAP V1
exits 1–13). The parts and instruments those gates need are spread over HARDWARE §3 and five
bring-up procedures, and three procedure rows depend on circuit values no document chose:
v1-ir check 3 measures "the design value of the driver resistor", v1-labtools records "pull-up
values" without a target, and V1 exit 8 and v1-modules check 14 test MISO release by a card
breakout that no document specified. Nothing has been bought.

**Decision.**
1. **One list.** `docs/bringup/v1-parts.md` is the buying and borrowing list for the
   breadboard prototype. Each row names the procedure that needs it; part numbers are examples
   of a class. It adds no requirement to a procedure, and a procedure's setup table still
   records the part actually used.
2. **IR driver:** GPIO 47 → 1 kΩ → NPN base, 10 kΩ base pull-down, LED and 33 Ω from 5 V (USB)
   into the collector: about 105 mA peak, 35 mA average in a mark at 33 % carrier duty. The
   receiver gets the datasheet's 100 Ω / 4.7 µF supply filter. 5 V touches the LED and the
   collector only.
3. **microSD breakout:** 3.3 V direct-connect, one that releases MISO when deselected; the
   regulator-and-buffer 5 V modules are excluded because SPI2 is shared (HARDWARE §2.3).
4. **I²C pull-ups:** the internal pull-ups stay enabled (`board_devkitc1.c`); one 4.7 kΩ pair
   is fitted unless a breakout provides pull-ups, every pull-up present is recorded, and the
   parallel value is kept at 2 kΩ or more.
5. **ID EEPROMs** are programmed off the device at 3.3 V (A0 strap for slot 1, WP to GND); a
   programmer that drives the chip at 5 V is excluded until measured at 3.3 V.
6. **Logic analyser rule:** ≥ 100 MS/s for the lab-tool checks; a 24 MS/s analyser is enough
   for the IR, NFC, module and Sub-GHz procedures.
7. **3.3 V only** on every GPIO and bus, as the procedures already require.

**Alternatives rejected**

| Option | Why rejected |
| --- | --- |
| Driving the IR LED straight from GPIO 47 | 20–40 mA at most from a pad; range and the v1-ir current check need a driver |
| IR LED from 3V3 | Up to 100 mA pulses on the DevKit's 3V3 regulator, which also feeds the panel backlight (HARDWARE §3.1) |
| No external I²C pull-ups | About 45 kΩ cannot meet 400 kHz rise times (v1-labtools check 21) |
| Leaving parts to the person doing the bring-up | Three checks would measure against values nobody chose |

**Consequences.** HARDWARE §3 points to the list. The procedures are unchanged; the values in
§3 of the list are what their checks measure against. A value that fails its check is
changed in a new task and recorded here, never tuned silently.

**Revisit trigger:** the first bring-up reading of the IR current, an I²C rise time, or MISO
release; or the V1 PCB schematic (HARDWARE §8), which replaces these breadboard values.

## ADR-0025 — One firmware, many targets: a capability matrix, the HAL seam first, ESP32-S3 the first port; feature scope within the use policy; clean-room-first licensing

**Status:** accepted (2026-11-27). The owner delegated product decisions, asked to absorb the
feature space of comparable tools (Flipper Zero, Cardputer/Bruce, Wi-Fi Pineapple) into **one
firmware whose features follow the board and architecture**, and chose each fork below in chat.
Recorded here rather than taken silently. ADR-0001 (MCU) and ADR-0009 (capability bits) are the
decided approaches this extends; neither is reversed.

**Context.** The request is "a single piece of software, not a version per platform, where the
features a board supports are the features it has." STARK ONE is already built this way: one
ESP-IDF build, `stark_board` capability flags (ARCHITECTURE §6.1), module capability bits and
`caps_required` gating (ADR-0009), and a pure-core / thin-port split (ARCHITECTURE §2). What the
request adds is twofold — (a) **other architectures** (STM32, RP2350, a Linux SoC), which today's
port layer deliberately does not abstract ("it exists so L2 cores and host tests can be built
without ESP-IDF, not to abstract all of ESP-IDF", ARCHITECTURE §6.2); and (b) **features we do
not yet have** (Wi-Fi/BLE, 125 kHz RFID, 1-Wire, USB-HID). A single *binary* cannot run on both a
bare-metal MCU and a Linux SoC; the achievable shape is **one code base, capability-gated, with
several build targets**, the pure cores unchanged and a thin port + board package per target.

**Decision.**
1. **The capability matrix is the contract.** A feature is present on a build iff its board/module
   declares the capability it needs (`stark_board` flags and ADR-0009 bits). No `#ifdef` per
   feature in app or core code, as ADR-0010 already forbids for the simulator. A feature a target
   lacks is dimmed and unlaunchable, never compiled out of the shared logic.
2. **The HAL seam before any new architecture.** The first and prioritised step is to draw a
   board-neutral hardware interface (the existing `stark_hal`/`stark_board`/`stark_storage_medium`
   seam, generalised to cover radio, network and USB transports) and keep **ESP32-S3 as the one
   and only port behind it**. Behaviour does not change; the boundary is made explicit and
   host-tested. Only after the seam is clean and proven does a second port (STM32/RP2350/Linux)
   become a milestone of its own. We do **not** start multi-architecture porting now.
3. **New features are in scope only within `docs/SECURITY_SCOPE.md`.** The accepted families are
   **Wi-Fi/BLE analysis (receive-first)**, **125 kHz RFID and 1-Wire of our own tags/devices**,
   and **USB-HID / "BadUSB" on our own, consenting machines**. The use policy's §3 exclusions are
   unchanged and explicitly rule out the offensive half of the reference tools: **deauthentication,
   jamming, flooding, beacon spam, evil-portal credential capture, identifier cloning/spoofing**
   are non-features and a PR implementing one is rejected regardless of quality. SECURITY_SCOPE §2
   gains the three families with the same receive-first, explicit-confirm, own-devices guardrails
   the radios already carry.
4. **Licensing: clean-room first, GPL fallback (non-commercial, educational).** The reference
   firmwares are copyleft — Flipper Zero and Proxmark3 **GPL-3.0**, Bruce **AGPL-3.0** (and it
   bundles GPL-3 Flipper code and ST-SLA NFC drivers). We **reimplement protocols from published
   specifications and our own analysis**, as we did for NEC/RC5/NDEF/CC1101, and do not copy their
   source. Where a protocol is impractical to reimplement and a GPL implementation is the only
   sensible route, we **may** vendor it and carry its licence: the project is personal/educational,
   not a commercial product, so GPL/AGPL obligations are acceptable; each such import is its own ADR
   naming the source and licence. Permissively-licensed **data** is used directly — the Flipper IR
   database is CC0-1.0 (`UberGuidoZ/Flipper-IRDB`) / MIT (`flipperdevices/irdb`) and may be imported
   with attribution.
5. **Reference, not dependency.** The repos above are read for behaviour and file-format shape
   (`.sub`/`.ir`/`.nfc` are formats, not code); Proxmark3 is the RFID/NFC reference, ESP32-Marauder
   the Wi-Fi/BLE **analysis** reference (its own docs note ESP-IDF cannot emit deauth frames, which
   matches our receive-first rule). None is a build dependency.

**Alternatives rejected**

| Option | Why rejected |
| --- | --- |
| One binary for every architecture | Impossible: an MCU image and a Linux-SoC program are different artifacts. "One code base, many targets" is the real form |
| Start multi-architecture porting now | The seam is not yet drawn; porting before the boundary is clean means porting a moving target. Seam first, one port, then expand (owner's choice) |
| Per-platform forks (a Flipper build, a Cardputer build) | Exactly the "a version per platform" the owner ruled out; the capability matrix gives one code base instead |
| `#ifdef STARK_HAS_WIFI` through app code | Breaks ADR-0010's no-build-flag rule and the single-logic goal; capability gating at run time is the established pattern |
| Vendor Flipper/Bruce wholesale to move fast | AGPL/GPL entanglement, ST-SLA drivers, and a large unread codebase; clean-room keeps the tree ours and the option open |
| Add the offensive Wi-Fi suite (deauth, evil portal, beacon spam) | SECURITY_SCOPE §3: non-features, rejected regardless of quality |

**Consequences.** ROADMAP gains a "Platform — HAL seam & capability matrix" track (ESP32-S3 first
port) and V2's candidate scope names the three feature families with their scope notes. TASKS opens
the hardware-free seam tasks first (STARK-0901+); each feature family becomes its own milestone when
reached, built behind a transport seam and emulator-verified like the V0.3–0.7 radios, with physical
validation deferred to V1/HIL. ARCHITECTURE §6.2's "small port layer" note gains a forward pointer
here. No code behaviour changes in this ADR's own change set — it is the plan.

**Revisit trigger:** the first non-ESP32 port attempt (does the seam hold?); any reference import
that cannot be reimplemented from a spec (opens its own licence ADR); or a feature request that
tests the SECURITY_SCOPE boundary (evaluated against §3 before it becomes a task).
## ADR-0026 — clang-tidy over the pure sources, in the lint job, with a named-check suppression rule

**Status:** accepted (2026-11-27). Resolves the `clang-tidy` item in the TASKS.md infrastructure
debt register (promised in TESTING §3/§7, "not yet active"). STARK-0808. The owner delegated tooling
decisions and asked for autonomous progress.

**Context.** TESTING §3 names `clang-tidy` (bugprone/cert/readability subset) as a build-quality gate
but marked it "not yet active": it needed a compile database clang can digest for our flags, or a
host-side run over pure sources only. The host test build already emits `compile_commands.json` for
exactly the pure cores (ARCHITECTURE §2, `test/host`), with no ESP-IDF in the include path — the
missing piece was a runner and a policy, not new infrastructure. Run broadly, clang-tidy's
readability family floods the output (1 274 identifier-length, 792 literal-suffix, 674 magic-number
findings) against choices ARCHITECTURE §4 and the ILP32-target `%u`/`uint32_t` idiom make
deliberately; the real signal (cert-err33, redundant declarations, a shared-bit style slip) is a few
dozen findings under it.

**Decision.**
1. **Scope: the pure cores.** `scripts/tidy.sh` configures the host build, derives the exact file
   list and flags from its `compile_commands.json` (one entry per source), and runs `clang-tidy` over
   the files under `components/`, `apps/`, `main/`. The ESP-IDF ports are not host-compilable and are
   not checked; `--list` prints both sets. This is the same core/port boundary `check_layers.py` and
   the host build already enforce (docs/PLATFORM.md).
2. **Checks: `bugprone-*`, `cert-*`, `readability-*`**, minus the families that encode house style or
   the LP64-host/ILP32-target mismatch, each disabled with its reason in `.clang-tidy`
   (`identifier-length`, `magic-numbers`, `uppercase-literal-suffix`, `function-cognitive-complexity`,
   `avoid-nested-conditional-operator`, `isolate-declaration`, `redundant-casting`,
   `non-const-parameter`, `implicit-widening-of-multiplication-result`, `easily-swappable-parameters`).
   `WarningsAsErrors: '*'`.
3. **Suppression rule.** A finding is fixed, or suppressed in-line as
   `/* NOLINTNEXTLINE(<check>): <reason> */` — the check named, a reason given. A bare `NOLINT` is not
   allowed (it is the clang-tidy analogue of TESTING §3's "no `-Wno-*`, no reasonless pragma").
4. **In the lint job.** `scripts/tidy.sh` runs in CI's `lint` job (ubuntu-24.04, clang-tidy 18.x, the
   same major as the committed `.clang-tidy`). A finding fails the build.

**Alternatives rejected**

| Option | Why rejected |
| --- | --- |
| The full `readability-*` family | ~2 700 findings against ARCHITECTURE §4 style and the target's integer idiom; it would train everyone to ignore the tool |
| A second compile database built just for tidy | The host build already emits the right one for exactly the cores; a second would drift |
| Checking the ESP-IDF ports too | They are not host-compilable; tidy would need the full IDF toolchain and would mostly report vendor-header noise (the `-Wundef` precedent, TESTING §3) |
| `clang-tidy` in the firmware build | Needs the Xtensa compile DB and IDF headers; the pure-core host run is where our logic lives and is cheap |

**Consequences.** TESTING §3 marks the gate active and the debt register entry is resolved. The
tree's existing findings were fixed in this change (mostly `(void)` on intentionally-ignored
`snprintf`, a few `NOLINTNEXTLINE` with reasons, two redundant declarations removed, a parameter-name
and a shared-`sizeof` tidy). New code is held to the same bar by the lint job. The size-regression
flag, the other debt-register item, is untouched.

**Revisit trigger:** a clang-tidy major-version bump that changes the finding set (re-verify on
ubuntu's pinned image), or a check whose value no longer justifies its suppressions.

## ADR-0027 — Wi-Fi/BLE analysis: an onboard-radio transport seam, receive-first by construction, two SoC capability bits

**Status:** accepted (2026-11-27). The owner prioritised Wi-Fi/BLE analysis as the first V2 feature
family (ADR-0025 decision 3) and chose receive-first, clean-room-from-spec. This ADR designs the
seam for it before any code; it extends ADR-0009 (capability bits) and ADR-0025, reverses neither.

**Context.** Unlike the Sub-GHz radio (an external CC1101 on a claimed module slot, ADR-0022), the
Wi-Fi and BLE radios are **integrated in the ESP32-S3 itself**. There is no slot to claim, no SPI,
no module descriptor to read — the radio is onboard, exactly as the IR LED/demodulator are onboard
(`stark_module_provide(STARK_MODULE_ONBOARD, …)`, `main/module_bridge.c`). Three questions follow:
how the capability is declared and gated when it is a property of the SoC rather than a plugged
part; where the pure/host-testable logic ends and the ESP-IDF radio code begins; and how
receive-first (SECURITY_SCOPE §2, guardrail 1) is made structural rather than a runtime check that a
future change could weaken. The analysis reference is ESP32-Marauder, whose own documentation notes
ESP-IDF cannot emit deauthentication frames — which matches, rather than limits, our receive-first
rule.

**Decision.**
1. **Two onboard capability bits, declared like IR, confirmed by the driver.** `STARK_CAP_WIFI`
   (bit 6) and `STARK_CAP_BLE` (bit 7) join `stark_caps.h`. They are **declared onboard** —
   `stark_module_provide(STARK_MODULE_ONBOARD, "WiFi"/"BLE", …)` — and **confirmed** only when the
   respective driver brings the radio up (`esp_wifi` / the BLE controller init succeeds). This reuses
   the existing model unchanged (a descriptor declares, a driver confirms, only a confirmed bit
   satisfies `caps_required`, ADR-0021); no new capability mechanism is introduced for "SoC
   features". The bits land with their confirming driver (the Wi-Fi/BLE radio tasks), not in a
   planning or pure-core change, so `check_caps.py`'s invariant — every bit an app names in
   `caps_required` is a defined bit, and `STARK_CAPS_KNOWN` equals the defined bits — holds at
   every commit.
2. **The transport seam is the only code that calls the radio stack.** `components/stark_wifi_transport`
   (and, separately, `components/stark_ble_transport`) hold every `esp_wifi` / BLE-controller call —
   promiscuous-mode receive, channel set, the RX callback that hands raw frames to the pure core.
   Nothing else in the tree includes an ESP-IDF radio header. This is the CC1101-transport pattern
   (ARCHITECTURE §2, the subghz seam) applied to an onboard radio: the layering check keeps the pure
   cores clean, and the emulator substitutes the seam with a fake that injects captured frames.
3. **Receive-first by construction, not by flag.** The transport seam exposes **no transmit, inject,
   or raw-send function at all** — the API is open/set-channel/start-RX/take-frames/stop-RX. There is
   no transmit path to gate, rate-limit, or confirm, because none exists in the type. This is
   stronger than the Sub-GHz transmit policy (which must police a TX path that does exist): here the
   offensive half (deauth, beacon/SSID flood, evil-portal, any frame injection, SECURITY_SCOPE §3) is
   unreachable because it is unimplemented and unexposed. A PR adding a Wi-Fi/BLE transmit primitive
   is a SECURITY_SCOPE §3 change and is rejected on that basis, not merely reviewed.
4. **The pure cores are the dissectors and the survey, host-tested from published specs.**
   `components/stark_wifi` parses IEEE 802.11 MAC frames (frame-control type/subtype, addresses,
   beacon/probe tagged parameters: SSID, DS channel, supported rates) and aggregates a per-channel
   survey; `components/stark_ble` parses BLE advertising PDUs (PDU type, AdvA, AD structures). Both
   take a byte buffer and produce a parsed struct — no ESP-IDF, no I/O — and are verified host-side
   with spec-derived vectors, as `subghz_decode`/`ndef` are. Captures and the survey log go to the
   user's own card (`logs/…`), never the console, and the dissectors **drop, never store, another
   network's credential material** (EAPOL key data, passphrase-bearing fields): they are classified
   and counted, their payload discarded (SECURITY_SCOPE §2 Wi-Fi/BLE note).
5. **Milestone shape.** The family opens as STARK-1001+ behind this seam and is **emulator-verified
   without physical hardware** (a fake transport injecting spec-derived frame captures), exactly as
   the V0.3–0.7 radios were; on-air validation is deferred to a V1-style HIL bring-up. The pure
   dissector tasks (1001–1003) need no hardware and no radio stack at all.

**Alternatives rejected**

| Option | Why rejected |
| --- | --- |
| A new "SoC capability" mechanism distinct from module bits | Unnecessary: IR is already an onboard-declared, driver-confirmed capability. Reusing it keeps one model and one checker |
| A transmit primitive behind a confirm + policy (as Sub-GHz) | Invites the §3 offensive features into the type. Receive-first is enforced best by **not having** a send path, not by guarding one |
| Put `esp_wifi` calls in the pure core and `#ifdef` the host build | Breaks ARCHITECTURE §2 layering and ADR-0010's no-build-flag rule; the dissectors would stop being host-testable |
| One combined `stark_wireless` component for Wi-Fi + BLE | Different radios, stacks, frame formats and lifecycles; two seams + two cores keep each host-testable and independently gated, one cap bit each |
| Store full captures including EAPOL/credential frames for "completeness" | SECURITY_SCOPE §2/§3: credential material of networks we do not own is dropped, not written. Analysis counts and classifies; it does not harvest |

**Consequences.** `stark_caps.h` will gain two bits (with their confirming drivers, not here).
ROADMAP's V2 gains a "Wi-Fi / BLE analysis (receive-first)" milestone with exit criteria; TASKS opens
the `# Wi-Fi/BLE analysis` section (STARK-1001+), the pure dissectors first. SECURITY_SCOPE already
carries the family (§2) and its exclusions (§3) from ADR-0025; no scope text changes here. No code
behaviour changes in this ADR's own change set — it is the plan and the seam contract.

**Revisit trigger:** the first on-air bring-up (does the promiscuous seam hold the pure/port line?);
any analysis feature that cannot be done receive-only (evaluated against SECURITY_SCOPE §3 before it
becomes a task); or a BLE stack choice (NimBLE vs Bluedroid) that forces a sub-decision (its own ADR).
