# Validation — tiers, the free emulator, and the coverage matrix

**Owner decision (final): STARK ONE never depends on a paid Wokwi plan.** Every
mandatory check is free and runs both locally and in GitHub Actions. Wokwi remains an
optional, supplemental simulator. ADR-0017 records the decision; this document is the
working reference.

Two phrases are used precisely throughout:

* **logic verified** — the production code path was exercised and its observable
  behaviour asserted (on the host, or on the real firmware in the emulator);
* **physical peripheral verified** — a real device (panel glass, switch contact,
  piezo, LED) was observed doing the thing. Only hardware (HIL, V1) produces this.

---

## 1. Tiers

| Tier | What | Runs | Mandatory | Proves |
| --- | --- | --- | --- | --- |
| 1 | Host unit and integration tests (`scripts/test_host.sh`) | macOS + Linux, Unity, ASan/UBSan, coverage | yes | Pure cores, deterministic fakes (fake HAL clock/GPIO), rendering into memory surfaces, input logic, UI/app logic that can run on a test port |
| 2 | Real ESP-IDF build (`scripts/build.sh --docker`) | pinned `espressif/idf:v6.1` | yes | Target compilation, component graph, `sdkconfig`, partitions, warning policy, binary/resource accounting, the merged flash image |
| 3 | Espressif esp-emulator (`scripts/test_emu.sh`, `test/emu/*.toml`) | pinned esp-emu, locally and in CI | yes | The **production artifact** booting through the real mask ROM, 2nd-stage bootloader and `app_main` on a dual-core ESP32-S3 model: FreeRTOS, timers, event concurrency, the input → event → UI → app path, UART logs, heap, panics |
| 4 | External-peripheral behaviour | host fakes/test ports now; HIL at V1 | as each applies | A: host fake/model tests; B: an integration harness running production logic on a test port; C: physical HIL once hardware exists |
| 5 | Wokwi (`scripts/test_wokwi.sh`, `.github/workflows/wokwi.yml`) | manual dispatch or the `wokwi` PR label, free quota only | **no** | Extra confidence (e.g. a simulated ILI9341 screenshot) when minutes happen to be available |

Rules:

* **One artifact.** No simulator-specific production code: no `#ifdef WOKWI`, `QEMU`,
  `ESP_EMU` (ADR-0010; `scripts/check.sh` enforces it). Test adapters live outside the
  firmware — host test ports under `test/host/support/`, emulator control in
  `scripts/test_emu.py`.
* **Performance is never gated below hardware** (ADR-0011). The emulator's clock is
  modelled, not measured; its timings are informational at most.
* A Wokwi failure is never hidden and never blocks a merge; a Wokwi pass is never the
  only evidence for a mandatory criterion.

---

## 2. esp-emulator — what was verified, and what it cannot do

Pinned in `scripts/esp_emu.lock`: **esp-emu 0.44.0** (Apache-2.0,
<https://github.com/espressif/esp-emulator>), release artifacts for Linux x86_64,
Linux arm64 and macOS Apple Silicon, each with its SHA-256 (checked against the
release's `SHA256SUMS` and GitHub's asset digests). `scripts/install_esp_emu.sh`
re-verifies the checksum before every extraction and fails on a mismatch. A version
bump is a deliberate change with a full emulator run — never "latest".

**Verified with this project's firmware (STARK ONE, ESP-IDF v6.1, 2026-09-27):**

| Capability | Result |
| --- | --- |
| ESP32-S3 boot: real ROM → IDF bootloader → `app_main`, dual core | ✅ `Multicore bootloader`, `Multicore app`, both cores started |
| Flash size from the image header (16 MB) | ✅ `SPI Flash Size : 16MB` |
| UART0 console at the IDF log format | ✅ every `boot:`/`board:`/`display:`/`app:`/`diag:` line |
| FreeRTOS tasks, `esp_timer` (5 ms input sampling), event bus across tasks | ✅ key actions published by the timer task, dispatched by the UI task |
| GP-SPI + GDMA transfers complete (the band pipeline's DMA/ISR handshake) | ✅ `display: full refresh` returns — transfers finish, no timeout |
| Heap accounting | ✅ `diag: heap=315352`, identical to the Wokwi figure for the same build |
| GPIO controller: inputs with pull-ups read released | ✅ `GPIO_IN` reads `0xffd9ffff` with the six keys high; no spurious key events |
| GDB remote stub (breakpoint, `set var`, continue) on core 0 | ✅ used for key injection (below) |
| Emulated-time scheduling independent of host speed | ✅ presses land at the same `now_ms` on every run |

**Not available, and not faked:**

| Limitation | Consequence |
| --- | --- |
| No external-device models on GP-SPI — **the ILI9341 is not emulated**. Transfers complete into nothing. | The emulator proves the render pipeline runs, never what a panel would show. Pixels are validated on the host at the display-port boundary (Tier 1/4B); panel orientation, colour order and glass are HIL. |
| **No way to drive an external GPIO pad.** A GDB write to `GPIO_IN` does not stick (the model computes it from pad state); no CLI/API for pad stimulus. | Key presses are injected one step later — see "Input injection". The pad → key-bit step is host-tested; wiring and bounce are HIL. |
| GDB breakpoints in code running on **core 1** are never reported (0.44.0): the core stalls silently. | Injection uses the input service on core 0 (`esp_timer` task, default affinity). Nothing on the UI task (core 1) can be probed with breakpoints — so no frame capture in the emulator. Should the timer task move cores, every input scenario fails loudly (the schedule never completes), it cannot pass falsely. |
| LEDC/buzzer waveform, status-LED level and backlight are not observable from outside. | Buzzer/LED/backlight logic stays host- or HIL-verified (§3.2). |
| Timing is modelled. | Informational only; performance gates stay on hardware at V1 (ADR-0011). |

**Input injection.** `scripts/test_emu.py` sets a breakpoint on the first statement of
`input_core_update()` and, while the emulated `now_ms` lies inside a scenario's press
window, ORs the key's bit into `raw_bitmap` before continuing. That value is exactly
what `input_sample_raw()` returns for a pressed key, so everything downstream is the
production artifact: debounce/long/repeat core, event publishing, `key:` logs, bus,
UI, menu, app lifecycle, rendering. The loop runs to the scenario's `end_ms`, so the
schedule is in emulated time; the harness then checks the serial log (ordered
`expect` lines, forbidden output, the heap gate) and fails if the schedule did not
reach its end.

---

## 3. Coverage matrix

### 3.1 The three V0 scenarios (previously mandatory in Wokwi)

Categories: **1** host test · **2** emulator · **3** host + emulator · **4** physical
peripheral, deferred to HIL.

**`v0-boot`** → `test/emu/v0-boot.toml`

| # | Wokwi assertion | Cat. | Replacement evidence |
| --- | --- | --- | --- |
| 1 | `board: devkitc1 pins ok spi2 ready` | 2 | emu `expect[0]`, production artifact |
| 2 | `display: init 320x240 band=40 bufs=2x25600` after it | 2 | emu `expect[1]` — the driver's init sequence and both DMA buffers; the ILI9341 accepting it is **4** (HIL, V1) |
| 3 | `app: registry n=4 about,inputtest,displaytest,buzzertest` | 2 | emu `expect[2]` |
| 4 | `boot: ui_ready in <N> ms` after the above (value not asserted) | 2 | emu `expect[3]` |
| 5 | `diag: heap=` after it | 2 | emu `expect[4]` |
| 6 | runner: first `diag: heap=` ≥ 200 000 | 2 | emu `heap_min = 200000` (315 352 B) |
| 7 | runner: no `panic:` / `Guru Meditation` / `ui: render failed` | 2 | emu default `forbid` list (plus `abort() was called`, `Backtrace:`) |
| + | (new) the first full refresh completes | 2 | emu `expect[5]` `display: full refresh` — the SPI/GDMA handshake finishes |

**`v0-boot-and-menu`** (ROADMAP V0 exit 2) → `test/emu/v0-boot-and-menu.toml`

| # | Wokwi assertion | Cat. | Replacement evidence |
| --- | --- | --- | --- |
| 1 | `boot: ui_ready in ` | 2 | emu `expect` |
| 2 | DOWN → `menu: sel=1 "Buzzer Test"` | 3 | emu (injected at `input_core_update`, then core → bus → menu) + host `test_input_sample.c` (GPIO 5 low → DOWN bit) + `check_pins.py` (diagram ↔ pin map) |
| 3 | UP → `menu: sel=0 "About"` | 3 | as #2 (GPIO 4) |
| 4 | OK short → `app: start about` | 3 | as #2 (GPIO 15); the SHORT semantics are also host-tested (`test_input_core.c`) |
| 5 | BACK short → `app: stop about` | 3 | as #2 (GPIO 16) |
| 6 | runner: heap, no panic | 2 | emu `heap_min`, default `forbid` |
| — | the simulated push-button pulling a real pad low | 4 | HIL, V1 (switch wiring, pull-up, bounce — TESTING §5 item 5) |

**`v0-input-matrix`** → `test/emu/v0-input-matrix.toml`

| # | Wokwi assertion | Cat. | Replacement evidence |
| --- | --- | --- | --- |
| 1 | three DOWN presses, OK → `app: start inputtest` | 3 | emu `expect` (`menu: sel=1/2/3`, `app: start inputtest`) + host pad test |
| 2–19 | for UP, DOWN, LEFT, RIGHT, OK, BACK: `key: <K> press`, `key: <K> release`, `key: <K> short` in order | 3 | emu `expect` (all 18 lines, in order, logged by the production input service) + host `test_input_sample.c` (each of GPIO 4/5/6/7/15/16 maps to its bit, active-low, pull-up idle) + host `test_input_core.c` (press/release/short semantics, 100 % lines) |
| 20 | `app: stop inputtest` after BACK | 3 | emu `expect` |
| 21 | runner: heap, no panic | 2 | emu `heap_min`, default `forbid` |
| — | six physical switches, electrically | 4 | HIL, V1 |

Every assertion that was mandatory in Wokwi now has free replacement evidence or is
explicitly deferred to HIL. Mutation checks when the tier was introduced: five broken
scenarios (missing line, wrong order, missing press, forbidden output, heap gate)
all fail with a precise message, and a real firmware regression (UP/DOWN swapped in
`ui_menu.c`) fails `emu-v0-boot-and-menu` (`menu: sel=3` instead of `sel=1`).

### 3.2 V0 evidence that was gathered manually in Wokwi (task-level, never in CI)

| Evidence (task) | Now |
| --- | --- |
| Screenshots decoded against a pattern model: seams, partial = requested region, full ≡ partial (STARK-0016) | Band arithmetic host-tested (`test_display_bands.c`), primitives pixel-exact (`test_gfx.c`); screen-level pixels move to a host display test port (Tier 4B, before STARK-0101). Glass: **4** |
| Panel orientation / mirror / BGR (STARK-0015) | **4** — HIL checklist (TESTING §5 item 3) |
| Status LED heartbeat on GPIO 18 (STARK-0008 `expect-pin`) | Not observable in the emulator; **4** at V1. Logic is a thin `esp_timer` port |
| Buzzer notes start and stop (STARK-0020 VCD) | Not observable in the emulator; the deadline logic is a candidate for a host test port; audible output **4** |
| Heap after 10× launch/exit per app (STARK-0020) | Repeatable as an emulator scenario (heap lines), no longer Wokwi-only |

### 3.3 V0.1 and later

TASKS.md V0.1 reads with this mapping (ADR-0017): a "Wokwi scenario" is an emulator
scenario `test/emu/<name>.toml` asserting the same log lines; a "screenshot" is a host
render check at the display-port boundary; anything needing a real peripheral is
labelled **physical, deferred to HIL** in the task's evidence.

---

## 4. Hardware-in-the-loop (future tier, V1 prototype)

Not a V0.x prerequisite. When the breadboard exists (ROADMAP "V1 prototype"):

* automatic flashing (`esptool` from a runner attached to the DevKitC-1);
* UART assertions with the same `expect` lists the emulator scenarios use;
* GPIO verification and **button injection** by a second MCU or relay board driving
  the key pads (replacing the GDB injection point with the real pad);
* SPI/display checks: logic-analyser capture of the panel bus, and a camera or
  photodiode for glass-level smoke checks;
* buzzer and LED measured on a logic analyser (frequency, duration, duty);
* every performance gate from ROADMAP V1, measured, into `docs/measurements.md`.

---

## 5. Running it

```bash
scripts/test_host.sh            # Tier 1
scripts/build.sh --docker       # Tier 2 (also writes build/merged-binary.bin)
scripts/test_emu.sh             # Tier 3 — every test/emu/*.toml, logs in build/emu/
scripts/test_emu.sh test/emu/v0-boot.toml
```

Optional, only with free Wokwi quota: `WOKWI_CLI_TOKEN=… scripts/test_wokwi.sh`, or
the `wokwi` label / manual dispatch of `.github/workflows/wokwi.yml`.
