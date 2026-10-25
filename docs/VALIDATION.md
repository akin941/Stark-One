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
| SPI2 with a second device attached and nothing behind it (STARK-0201) | ✅ `sdmmc_card_init` fails at once: every production boot logs `storage: no card`, retries are silent, the UI stays responsive |
| Flash persistence across a reboot (STARK-0204) | ✅ `--save-state` writes the emulated flash (NVS included) back into the image when the run is terminated; booting it again loads `settings: … (nvs)`. A scenario's `control` "reset" therefore ends a phase and the next phase is a new esp-emu run over the same image |
| In-process chip reset over `--control-tcp` (STARK-0204) | ⚠️ the reset works, but the 0.44.0 GDB stub drops its connection, so key injection cannot continue across it — hence the phase-per-run mechanism above |
| `erase-region` over `--control-tcp` (STARK-0204) | ✅ `control: erased flash 24576 bytes at 0x9000` — the NVS partition wiped mid-run; sent fire-and-forget, because GDB holds the CPU while the command is written and the emulator answers only once it resumes |
| A "pulled" card's sector I/O failing (storage test image, STARK-0207) | ✅ the test medium re-registers the volume's diskio with WL calls gated on `emu_medium_present`: a pull mid-write fails the next sector write (`wtest failed: I/O error at 20480`) exactly as a removed card would |
| The production firmware through the RMT loopback (STARK-0304) | ✅ App Test's IR self-test: NEC, NECX, RC5, RC6, SIRC-12/15/20 each encoded → gated → RMT TX → loopback → RMT RX → decoded to the sent values, 7/7 (`v03-ir-loopback`); without the loopback each reports a timeout, no panic (`v03-ir-noloop`); heap Δ 0 over three runs. Baseband only — no carrier, no light |
| Saved signals replayed through the RMT loopback (storage test image, STARK-0306) | ✅ the FAT partition is seeded at build time with the spec-derived `lab_*.ir` files (ESP-IDF's `fatfs_create_spiflash_image`). IR → Browse → `lab_nec.ir` → confirm → `ir: tx NEC` → `ir: rx NEC addr=0x00 cmd=0x12`; that capture saved as `ir_0001.ir`, listed first and replayed; the RAW seed comes back as `ir: rx RAW n=13`; `lab_bad.ir` listed `(invalid)` and never sent; BACK on the confirm cancels; a send inside 1 s is refused (`v03-replay`). Baseband only |
| The production image on the empty bus (STARK-0402) | ✅ `nfc: no module (Not found)` right after `ir: init`; boot continues, no retry storm (`v04-nfc-boot`). Boot heap −2 428 B for the I²C driver and the PN532 state |
| The fake PN532 behind the test image's transport (STARK-0402) | ✅ `nfc: pn532 fw 1.6`: the production driver's detection sequence answered by `test/fake/fake_pn532.c` through the substituted `stark_nfc_transport` (`storage/v04-nfc-module`). Integration only, not RF |
| I²C master with nothing on the bus (V0.4 planning) | ✅ ESP-IDF's `i2c_basic` example on the esp32s3 model: `i2c_new_master_bus` succeeds and a transaction to an absent address returns `ESP_ERR_INVALID_RESPONSE` (0x108, ESP-IDF's address-NACK result) at once — no hang. esp-emu 0.44.0 has **no** external I²C device model, so the production image always sees an empty bus (ADR-0020) |
| RMT TX → RX loopback (`--rmt-loopback`, V0.3 planning) | ✅ `0:2` on esp32s3 (first TX → first RX channel): ESP-IDF's `ir_nec_transceiver` example decodes its own NEC frames; `0:4` delivers nothing. Baseband, exact durations, no carrier (ADR-0019 §2) |
| GDB writes to a RAM global (`pokes`, storage test image, STARK-0202) | ✅ `emu_medium_present` flips stick; removal and reinsertion land at the scheduled emulated time |
| Wear-levelled FAT on an internal-flash partition (storage test image, STARK-0201) | ✅ formatted on first boot, mounted at `/sd`: `storage: mounted emu-flashfat total=972 free=972` (kB) |

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
| 3 | `app: registry n=<N> <ids>` (the current registry) | 2 | emu `expect[2]` |
| 4 | `boot: ui_ready in <N> ms` after the above (value not asserted) | 2 | emu `expect[3]` |
| 5 | `diag: heap=` after it | 2 | emu `expect[4]` |
| 6 | runner: first `diag: heap=` ≥ 200 000 | 2 | emu `heap_min = 200000` (315 352 B) |
| 7 | runner: no `panic:` / `Guru Meditation` / `ui: render failed` | 2 | emu default `forbid` list (plus `abort() was called`, `Backtrace:`) |
| + | (new) the first full refresh completes | 2 | emu `expect[5]` `display: full refresh` — the SPI/GDMA handshake finishes |

**`v0-boot-and-menu`** (ROADMAP V0 exit 2) → `test/emu/v0-boot-and-menu.toml`

| # | Wokwi assertion | Cat. | Replacement evidence |
| --- | --- | --- | --- |
| 1 | `boot: ui_ready in ` | 2 | emu `expect` |
| 2 | DOWN → the next launcher item (`menu: sel=<n> "<label>"`, rows as the launcher lists them) | 3 | emu (injected at `input_core_update`, then core → bus → menu) + host `test_input_sample.c` (GPIO 5 low → DOWN bit) + `check_pins.py` (diagram ↔ pin map) |
| 3 | UP → back one row | 3 | as #2 (GPIO 4) |
| 4 | OK short → `app: start about` | 3 | as #2 (GPIO 15); the SHORT semantics are also host-tested (`test_input_core.c`) |
| 5 | BACK short → `app: stop about` | 3 | as #2 (GPIO 16) |
| 6 | runner: heap, no panic | 2 | emu `heap_min`, default `forbid` |
| — | the simulated push-button pulling a real pad low | 4 | HIL, V1 (switch wiring, pull-up, bounce — TESTING §5 item 5) |

**`v0-input-matrix`** → `test/emu/v0-input-matrix.toml`

| # | Wokwi assertion | Cat. | Replacement evidence |
| --- | --- | --- | --- |
| 1 | DOWN presses to Input Test (past the Tests header), OK → `app: start inputtest` | 3 | emu `expect` (`menu: sel=1/2/3`, `app: start inputtest`) + host pad test |
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
| Screenshots decoded against a pattern model: seams, partial = requested region, full ≡ partial (STARK-0016) | Band arithmetic host-tested (`test_display_bands.c`), primitives pixel-exact (`test_gfx.c`), and the real launcher on the host UI test port (§3.3): frame golden, every pixel drawn, identical frames at band heights 7 / 40 / 240, exact render area per interaction. Glass: **4** |
| Panel orientation / mirror / BGR (STARK-0015) | **4** — HIL checklist (TESTING §5 item 3) |
| Status LED heartbeat on GPIO 18 (STARK-0008 `expect-pin`) | Not observable in the emulator; **4** at V1. Logic is a thin `esp_timer` port |
| Buzzer notes start and stop (STARK-0020 VCD) | Not observable in the emulator; the deadline logic is a candidate for a host test port; audible output **4** |
| Heap after 10× launch/exit per app (STARK-0020) | Repeatable as an emulator scenario (heap lines), no longer Wokwi-only |

### 3.3 The host UI test port (Tier 4B)

`test/host/port/` runs the production `stark_ui`, `stark_app` launcher, list menu,
status bar and `stark_gfx` on the host against test backends: `stark_display` over a
320×240 RGB565 framebuffer, filled band by band with the production `display_bands()`
walk into poisoned band buffers (an undrawn pixel shows as magenta); `stark_event` over
the production core without locks; a `stark_buzzer` recorder; an `esp_log` capture;
the fake HAL clock. Shim headers (`esp_log.h`, `sdkconfig.h`) are visible only to
these targets, never to pure-core tests. Tests drive key events and assert on pixels,
render areas, captured log lines and buzzer calls — exactly what the firmware hands
to `stark_display_render()`, not what a panel shows.

`test_ui_launcher.c` (the V0 launcher): registry log; first frame is one full-screen
render with no undrawn pixel and matches its reviewed golden hash; an idle tick renders
nothing; DOWN repaints rows 0–1 with a click; the UP wrap repaints rows 0–3 (V0's
bounding-box damage, refined by STARK-0101); OK launches, BACK stops and restores the
selection; the launcher redrawn at band heights 7 and 240 is pixel-identical; BACK at
the root rejects with no render; a failed render is logged, not fatal. Building it
also exposed a latent defect — `ui_statusbar.c` used `NULL` without `<stddef.h>`,
compiling on target only through an ESP-IDF header — fixed with the port.

`test_ui_apps.c` pins the real Input Test (idle and key held), Buzzer Test and Display
Test frames; they proved STARK-0103's theme refactor pixel-identical.

A golden is recorded only after the rendered frame (`build-host/*.ppm`) was looked at;
a deliberate visual change updates it in the same commit. A one-pixel shift of the
menu text inset fails it.

### 3.4 V0.1 and later

TASKS.md V0.1 reads with this mapping (ADR-0017): a "Wokwi scenario" is an emulator
scenario `test/emu/<name>.toml` asserting the same log lines; a "screenshot" is a host
render check at the display-port boundary; anything needing a real peripheral is
labelled **physical, deferred to HIL** in the task's evidence.

### 3.5 V0.2 — storage and settings (ADR-0018)

V0.2 completion means **storage and settings software logic verified**; physical
microSD behaviour is never claimed below V1. Per exit criterion:

| Exit criterion | Host (Tier 1/4A) | Emulator, production image (Tier 3) | Emulator, storage test image (Tier 4B) | Physical, deferred to HIL (V1) |
| --- | --- | --- | --- | --- |
| 1. Settings survive reboot | settings core over a fake NVS with every failure switch | set in the app → control-channel reset → loaded and applied | — | power cycle on the breadboard |
| 2. 100 kB write/read-back while rendering | helpers on real POSIX I/O in a temp dir, streaming, CRC | — (no SD model) | worker streams 100 kB, byte compare + CRC-32, UI renders and handles a key meanwhile | real card on shared SPI2; band-flush stall measured |
| 3. Removal mid-write degrades | `EIO`/short write/`ENOSPC`/rename/fsync faults at every step | — | medium removed mid-write over GDB → error, alert, no panic, remount, temp swept | real pull and reinsertion; detection latency |
| 4. Mount failure leaves device usable | every mount result in the state machine | every boot is a no-card boot: all apps usable | unformatted medium → `unusable card` | no card / non-FAT / faulty card |

**The storage test image** (`test/emu/storage_image/`) builds the repository's `main`,
apps and components with only `stark_storage_medium` replaced by an internal-flash FAT
volume (wear-levelled partition, mounted at `/sd`) whose presence and "formatted" flags
the harness flips with GDB (`pokes`). Its partition table adds that FAT partition.
`scripts/check_emu_image.py` proves in CI that it differs from the production build in
exactly that component and the partition-table keys. It is the only firmware other than
the production merged image that CI runs, and production code contains no reference to
it (no conditional, no fake path, no test symbol).

### 3.6 V0.3 — infrared (ADR-0019)

V0.3 completion means **IR software / protocol verified**; optical IR is never claimed
below V1.

**The esp-emu RMT loopback, exactly.** `--rmt-loopback 0:2` on the esp32s3 model feeds
the first RMT TX channel's symbols into the first RX channel (verified with ESP-IDF's
`ir_nec_transceiver` example: the sent NEC address/command decoded back; the TRM index
`0:4` delivers nothing). What arrives is the **pre-carrier baseband** with exact
durations. It proves the production firmware's encoder, RMT TX, RMT RX, symbol
conversion, decoder, events, app and storage work together. It does **not** prove a
carrier was generated, light left an LED, a receiver demodulated it, or that timings
survive real hardware — tolerance is proven on the host with jittered spec-derived
vectors.

**Status (STARK-0307): every software half met** — evidence in docs/measurements.md
"V0.3 exit criteria"; every physical half open in `docs/bringup/v1-ir.md`.

| Exit criterion | Host | Emulator (production / storage image, loopback) | Physical, deferred to HIL (V1) |
| --- | --- | --- | --- |
| 1. ≥ 20 vectors per protocol | ≥ 20 spec-derived vectors per protocol, tolerance edges, malformed frames, exhaustive round trips | every protocol encoded → loopback → decoded | ≥ 20 vectors recorded from our own remotes |
| 2. Capture, save, replay | name generation, raw format | capture → save → browse → replay → captured again; transmit gated, confirmed, logged | own remote → receiver → save → LED → own device responds |
| 3. Raw replay | raw format round trip + fuzzing | RAW seed replayed and captured back | an unsupported own remote |

### 3.7 V0.4 — NFC (ADR-0020)

V0.4 completion means **NFC software / protocol verified**; reading a real tag is never
claimed below V1.

**What runs where.**
* **Host:** the pure NFC core (NDEF, PN532 framing and protocol, Type 2, identification,
  polling, reader, writer, dump format) against spec-derived vectors, and the
  production driver against the fake PN532 (`test/fake/fake_pn532.c`). The fake answers
  I²C transactions like the chip, including the ready byte, ACK/NACK, checksums, the
  application-error frame and a simulated tag's memory. It injects NACKs, checksum
  errors, truncation, timeouts, bus errors, wrong response codes and tags appearing,
  disappearing or vanishing mid-transaction.
* **Emulator, production image:** an empty I²C bus, so the evidence is the no-module
  path only.
* **Emulator, test image:** `stark_nfc_transport` is substituted by one built on the
  same fake (as `stark_storage_medium` is, ADR-0018 §3); GDB pokes choose the tag
  (`emu_nfc_tag`) and faults (`emu_nfc_fault`). This proves the production driver, NFC
  task, app, UI and storage work together over a modelled PN532.
* **Not proven anywhere before V1:** PN532 electrical I²C, the RF field and antenna,
  ISO 14443 timing, range and orientation, real tags.

**Status (STARK-0405): every software half met** — evidence in docs/measurements.md
"V0.4 exit criteria"; every physical half open in `docs/bringup/v1-nfc.md`.

| Exit criterion | Host | Emulator (production / test image) | Physical, deferred to HIL (V1) |
| --- | --- | --- | --- |
| 1. UID and technology, ≥ 20/20 reads | ≥ 20 tag vectors identified; 20/20 consecutive reads through driver + fake | 20/20 arrive/leave cycles per Type 2 tag over the fake transport | 20/20 reads of each of our own tag types |
| 2. NDEF URI write + read-back | writer + driver + fake: written, read back byte-identical, parsed; refusals | confirmed write → read back → same URI over the fake transport | our own NTAG, read back by the device and a phone |
| 3. Module absence | absent, silent, NACKing, corrupt modules; recovery | production image on an empty bus: `nfc: no module`, app disabled | board without the module; unplugged while running |

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
* the V0.2 storage gates deferred by ADR-0018 (ROADMAP V1 exit 8, TESTING.md §5): real
  microSD write/read-back, pull-mid-write, card detection, panel + SD bus sharing.

---

## 5. Running it

```bash
scripts/test_host.sh            # Tier 1
scripts/build.sh --docker       # Tier 2 (also writes build/merged-binary.bin)
scripts/test_emu.sh             # Tier 3 — every test/emu/*.toml, logs in build/emu/
scripts/test_emu.sh test/emu/v0-boot.toml
scripts/test_emu.sh test/emu/soak/v01-soak.toml   # the 60 s soak (~8 min wall)
scripts/build.sh --docker --emu-storage            # the storage test image (ADR-0018)
python3 scripts/check_emu_image.py                 # ... differs only in stark_storage_medium
scripts/test_emu.sh --image storage                # every test/emu/storage/*.toml
```

Optional, only with free Wokwi quota: `WOKWI_CLI_TOKEN=… scripts/test_wokwi.sh`, or
the `wokwi` label / manual dispatch of `.github/workflows/wokwi.yml`.
