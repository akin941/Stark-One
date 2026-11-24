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
to V0.6. STARK-0701 to STARK-0708 build it; the first (pin safety and the lab seam) has landed, see the
addendum at the end.

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
