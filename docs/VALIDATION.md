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
| I²C master with nothing on the bus (V0.4 planning) | ✅ ESP-IDF's `i2c_basic` example on the esp32s3 model: `i2c_new_master_bus` succeeds and a transaction to an absent address returns `ESP_ERR_INVALID_RESPONSE` (0x108, ESP-IDF's address-NACK result) at once — no hang. esp-emu 0.44.0 has **no** external I²C device model, so the production image sees no PN532 (ADR-0020). **Corrected at STARK-0502:** the bus is not completely empty. `i2c_master_probe` and a read at **0x50** succeed and read all zeros; 0x10, 0x24, 0x51 and 0x52 NACK. The behaviour is undocumented and has no CLI option. A descriptor-less device at 0x50 is what the production image's slot 0 sees (`generic (bad magic)`) |
| RMT TX → RX loopback (`--rmt-loopback`, V0.3 planning) | ✅ `0:2` on esp32s3 (first TX → first RX channel): ESP-IDF's `ir_nec_transceiver` example decodes its own NEC frames; `0:4` delivers nothing. Baseband, exact durations, no carrier (ADR-0019 §2) |
| GDB writes to a RAM global (`pokes`, storage test image, STARK-0202) | ✅ `emu_medium_present` flips stick; removal and reinsertion land at the scheduled emulated time |
| GPIO output read back as an input (`GPIO_MODE_INPUT_OUTPUT`, STARK-0701 probe, 2026-11-24) | ✅ on pads 38, 39, 40 and 3: driven 0 reads 0, driven 1 reads 1; `gpio_set_drive_capability(GPIO_DRIVE_CAP_0)` then `gpio_get_drive_capability` returns `GPIO_DRIVE_CAP_0`. The lab's contention check (drive, wait, read back) therefore runs for real. The drive strength itself has no observable effect |
| GPIO any-edge interrupt from a pad the firmware itself toggles (STARK-0701 probe) | ✅ 20 software toggles of pad 38 gave 20 ISR calls on pad 38 and none on pad 39 (no wire joins two pads). Edge capture of a self-driven pad is available to a test image; an *external* signal is still not |
| I²C sweep 0x08 to 0x77 and register access (STARK-0701 probe) | ✅ 112 `i2c_master_probe` calls (5 ms timeout each, about 3.2 ms of emulated time in all): exactly **0x50** answers; a write to 0x51 at 400 kHz returns `ESP_ERR_INVALID_RESPONSE` (264, the address NACK). The built-in EEPROM at 0x50 reads zeros **and accepts writes**: A5 5A written at register 0x10 is read back from register 0x10 (a plain read after a write returned zeros). Correction to the "reads zeros" fact above: the bus does not refuse a write there, so STARK-0704's refusal of writes to 0x50 is firmware's, never the bus's |
| The GPIO interrupt service and an any-edge handler (STARK-0703, `v07-measure-prod`) | ✅ `gpio_install_isr_service(ESP_INTR_FLAG_IRAM)`, `gpio_set_intr_type(GPIO_INTR_ANYEDGE)`, `gpio_isr_handler_add` with an `IRAM_ATTR` handler and the removal again all return OK on the production image; an unconnected pull-up pad raises no interrupt in a 20 ms gate and a 4 s window (the pad reads high throughout), so the Measure row answers `no signal (high)`; no `gpio:` error line. A pad the firmware toggles itself raised 20 of 20 interrupts in the STARK-0701 probe, but the GPIO app never measures a pad it drives, so no edge is ever stamped in the production image |
| The I²C app on the production image (STARK-0704, `v07-i2c-prod`) | ✅ The scan (112 `i2c_master_probe` calls through the lab transport, 5 ms each) logs `lab: i2c scan found 0x50`: **the whole list is 0x50**, so 0x10, 0x24, 0x51 and 0x52 NACK as the STARK-0701 probe found. A register read of 0x50 (8 bytes from register 0, one `i2c_master_transmit_receive` on a device handle attached for the call and removed after it) returns eight zeros, and the handle coexists with the module task's own handle at the same address. A write to 0x50 is refused by the service before the bus is touched (the emulator's EEPROM would take it), no confirmation is asked, and a read after it is zero again. No second device and no waveform exist in the emulator, so this is the real driver and transport on the one thing the model has, never evidence of a device |
| UART1 bridged to a host peer, `--uart1-tcp` (STARK-0705 probe, 2026-11-24, throwaway task on the production image, not committed) | ✅ on the **S3 model** (upstream verified it on C3 and P4 only). `uart_driver_install(UART_NUM_1, 4096, 0, 0, NULL, 0)`, `uart_param_config` (115 200 8N1) and `uart_set_pin(38, 39)` return OK; the emulator listens and the host peer connects (the bridge does not depend on the pins). The device's 64-byte pattern `00..3f` reaches the peer intact; the peer's 1 000, 3 000 and 20 000 bytes in one burst come back through the device's echo task identical, **none lost** (the driver's 4 kB buffer is drained by the task as it fills); the first 256 bytes of the reply arrive 174 µs of emulated time after the device sent. **No baud timing**: 20 000 bytes make the round trip in 0.08 s of wall time where 115 200 baud needs 1.7 s each way, so bytes arrive at once whatever baud is configured, and framing, parity and noise cannot be produced. The emulated clock runs at about 0.3× wall time, so a peer that answers within a millisecond of wall time answers within microseconds of emulated time. With no client connected TX is discarded (the harness fails a scenario whose peer cannot connect) |
| The UART app on the production image (STARK-0705, `v07-uart-prod`) | ✅ A host peer on `--uart1-tcp` (`uart1_peer = "echo"`: it sends back every byte it receives and nothing of its own). The monitor opens the real driver receive-only (no TX pad routed; the peer is silent) and BACK removes it and releases both pins; the loopback view asks the confirmation once, routes TX, sends the 64-byte pattern as two writes of 32 and reads it back **64/64 ok** at 115 200, then at 230 400 (the driver's baud changes) and at 230 400 8E1 (the driver is installed again, TX still routed; ESP-IDF logs `GPIO 38 is not usable, maybe used by others` there: its own reservation of the previous install); leaving removes the driver and releases both pins. This is the real driver, transport and service on the one thing the model has: byte transport. It never shows a baud rate, a level or an error (the model produces none) |
| Wear-levelled FAT on an internal-flash partition (storage test image, STARK-0201) | ✅ formatted on first boot, mounted at `/sd`: `storage: mounted emu-flashfat total=972 free=972` (kB) |

**Not available, and not faked:**

| Limitation | Consequence |
| --- | --- |
| No external-device models on GP-SPI — **the ILI9341 is not emulated**. Transfers complete into nothing. | The emulator proves the render pipeline runs, never what a panel would show. Pixels are validated on the host at the display-port boundary (Tier 1/4B); panel orientation, colour order and glass are HIL. |
| **No way to drive an external GPIO pad.** A GDB write to `GPIO_IN` does not stick (the model computes it from pad state); no CLI/API for pad stimulus. | Key presses are injected one step later — see "Input injection". The pad → key-bit step is host-tested; wiring and bounce are HIL. |
| GDB breakpoints in code running on **core 1** are never reported (0.44.0): the core stalls silently. | Injection uses the input service on core 0 (`esp_timer` task, default affinity). Nothing on the UI task (core 1) can be probed with breakpoints — so no frame capture in the emulator. Should the timer task move cores, every input scenario fails loudly (the schedule never completes), it cannot pass falsely. |
| LEDC/buzzer waveform, status-LED level and backlight are not observable from outside. | Buzzer/LED/backlight logic stays host- or HIL-verified (§3.2). |
| Timing is modelled. | Informational only; performance gates stay on hardware at V1 (ADR-0011). |
| **Pull resistors are not modelled** (STARK-0701 probe): an input configured with a pull-down, a pull-up or no pull reads **high** on every unconnected pad tried (38, 39, 40, 3), with the input buffer off or on, and a pad released after being driven low also reads high (floating = high) | The lab's pad-state probe (each pull for 2 ms, `stark_lab_state()`) can only report "driven high" for an unconnected pad on the production image, and a pull-down read never returns low. Host tests and the test image's pad model carry that logic; no acceptance criterion depends on it. The earlier fact stands: a pull-up read is high |
| **LEDC does not reach a pad** (STARK-0701 probe): a pad routed to LEDC (timer 2, channel 2, 10 bit) at 1 kHz 50 %, at 100 % duty and at 20 Hz read low in every sample (4 000 at 1 kHz, 40 000 at 20 Hz) with no pad interrupt, whether the input buffer was enabled by `gpio_set_direction` or not | The PWM output and the frequency and duty measurement of one pad against another (STARK-0702, 0703) are proven on the host and the test image only. LEDC itself configures (`ledc_timer_config`, `ledc_channel_config` return OK) |
| ESP-IDF, not the emulator (seen in the same runs): `gpio_config()` reserves a pad it makes an output and logs `gpio: conflict found for GPIO[n]` on every later output configuration of the pad, and LEDC logs `GPIO n is not usable, maybe conflict with others` for a reserved pad; `gpio_reset_pin()` revokes the reservation. A 20 Hz LEDC timer at 10 bit on an 80 MHz clock needs a divider above the 10-bit integer limit, and reconfiguring a configured timer for it fails with `timer clock conflict` | `stark_hal_gpio_*` lab pad calls track the pads they made outputs and reset them on release (no warning per drive); the PWM floor of 100 Hz in ADR-0023 §4 is the right one (80 MHz / (100 × 1024) = 781 < 1 024) |

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

### 3.8 V0.5 — module bus (ADR-0021)

V0.5 completion means **software / module-protocol verified**; a real module, connector
or shared-SPI signal is never claimed below V1.

**What runs where.**
* **Host:** the pure descriptor parser and discovery core against catalogue vectors from
  an independent encoder, and production discovery against the fake 24C02
  (`test/fake/fake_24c02.c`), which models address ACK/NACK and the word-address pointer
  and injects NACKs, timeouts, bus errors, mid-read removal and bit flips. Arbitration
  runs on an injected table with two wired slots, which this board does not have.
* **Emulator, production image:** esp-emu acknowledges 0x50 and reads zeros, and NACKs
  the other addresses tried (§2), so slot 0 is an unknown EEPROM and slot 1 has none.
* **Emulator, test image:** `stark_module_transport` is substituted with one built on
  the same fake; GDB pokes choose modules (`emu_mod_slot0/1`) and faults
  (`emu_mod_fault`). A test-only stand-in driver (`emu_mod_traffic`) runs real
  `spi_device` transfers on `MOD_CS`, with nothing on the other end.
* **Not proven anywhere before V1:** real 24C02 communication, pull-ups, the connector,
  supply limits, insertion and removal, hotplug safety, bus recovery, two modules on SPI
  with the panel, chip-select isolation, contention and timing, module power-up.

**Status (STARK-0505): exits 1 and 2 met in software; exit 3 is a V1 gate.** Evidence in
docs/measurements.md "V0.5 exit criteria"; every physical half is open in
`docs/bringup/v1-modules.md`.

| Exit criterion | Host | Emulator (production / test image) | Physical, deferred to HIL (V1) |
| --- | --- | --- | --- |
| 1. Plugging changes the app list | insertion, removal, replacement; launcher dims and restores | same image: reader and EEPROM change, NFC row follows, inspector follows | a real module plugged in |
| 2. Unknown/absent EEPROM | every corrupt vector generic, no capabilities; absent "no ID" | production: slot 0 generic, slot 1 no ID, boot unaffected | blank and absent chips on a connector |
| 3. Two modules share SPI | claims, chip selects never crossed, stale leases, two wired slots | one stand-in driver alongside rendering | **the gate itself**: two modules and the panel on a logic analyser |

### 3.9 V0.6 — Sub-GHz laboratory module (ADR-0022)

V0.6 completion means **software / RF-protocol verified**; a radio transmitting or
receiving is never claimed below V1.

**What runs where.**
* **Host:** the pure cores (CC1101 register profiles, the RSSI conversion and scan, the
  `rf` file format, the three OOK decoders, the transmit policy) against vectors from
  `scripts/gen_subghz_vectors.py`, an independent Python implementation; the production
  driver and service against the fake CC1101 (`test/fake/fake_cc1101.c`), which models the
  chip as seen over SPI (status byte, registers, `PATABLE`, strobes, `MARCSTATE`, an RSSI
  from an injected air, scenes played on GDO2, GDO0 recorded and optionally fed back);
  the app on the host UI port. Fault injection reaches every transfer of a send.
* **Emulator, production image:** slot 0 reads as an unknown EEPROM, so the radio is
  never declared, the row is dimmed and the boot is unaffected (`v06-subghz-absent`).
* **Emulator, test image:** `stark_subghz_transport` is substituted by one built on the
  fake (the fourth and last allowed substitution); GDB pokes choose the module
  (`emu_mod_slot0 = 32`), the chip, the air (`emu_sg_carrier_khz`, `emu_sg_level`), the scene
  (`emu_sg_scene`), the own receiver (`emu_sg_own_rx`) and faults (`emu_sg_fault`).
  `storage/v06-detect`, `-scan`, `-capture`, `-decode` and `-replay` each prove one stage
  through the production service, app, policy and storage; `soak/v06-soak` runs all of them,
  in order, twice, with the module lost and replugged in between.
* **Not proven anywhere before V1:** the CC1101 itself (SPI timing, `CHIP_RDYn`, the
  analogue register values), the production RMT receive and transmit paths on GDO2 and
  GDO0, RSSI against a real level, timing of a real remote, power, on-air time, spurious
  emissions, antenna and matching, the shared SPI bus with a real chip.

**Status (STARK-0606): every software half met** — evidence in docs/measurements.md
"V0.6 exit criteria"; every physical half open in `docs/bringup/v1-subghz.md`.

| Exit criterion | Host | Emulator (test image) | Physical, deferred to HIL (V1) |
| --- | --- | --- | --- |
| 1. RSSI/scan shows our own transmitter, tracks distance | RSSI conversion vectors; the sweep finds the injected carrier's channel | `v06-scan`: the meter follows `emu_sg_level`, the scan peaks at the carrier's channel | our own transmitter at its frequency, the level falling with distance |
| 2. Raw capture reproduces documented timing | spec-derived scenes captured, saved, parsed back, measured within ±1 symbol tick, decoded | `v06-capture`, `v06-decode`: fingerprints equal the generator's | a capture of our own remote against its datasheet |
| 3. Replay triggers our own receiver | the replay leaves GDO0 symbol for symbol; the own receiver captures it back | `v06-replay`: 192 symbols with the file's fingerprint | our own receiver reacts |
| 4. TX impossible without confirmation, duty cap respected | every refusal path; `PATABLE` only ever 0x60; two simulated hours of a greedy sender | `v06-replay`: boot lock, cancel, one send, off-time refusal | power and on-air time on a spectrum analyser |

### 3.10 V0.7 — lab tools (ADR-0023)

V0.7 completion means **software / lab-protocol verified**: a pad driven, a wire measured or a
real peripheral answering is never claimed below V1 (ROADMAP V1 exit 13). Landed so far:
STARK-0701 (pin safety and the lab seam), STARK-0702 (the GPIO app), STARK-0703 (frequency and duty
measurement), STARK-0704 (the I²C scanner), STARK-0705 (the UART tool) and STARK-0706 (the SPI probe); the self-test follows in STARK-0707.

**What runs where (STARK-0701).**
* **Host:** the board's pin classes for every number from -1 to 64 and the offer table derived
  from the pin map (`test_board_pins`); the test-only pad model (`test_fake_lab`: hi-Z, pulls,
  outputs, external sources, wires, faults); the pure pad decisions over the real module core and
  the fake 24C02 (`test_lab_pins`: the offer for each slot state, acquire, the arm, contention,
  the state probe, hot-plug withdrawal, a sweep of every service call against every number); the
  production transport over the fake HAL (`test_lab_service`: the sweep again at the transport).
  `lab_pins.c`, `lab_transport_hal.c`, `fake_lab.c` and the new part of `stark_board.c` run at
  100 % lines under ASan and UBSan.
* **Emulator, production image:** the lab starts after the module table and logs the offer:
  slot 0 reads generic (esp-emu answers at 0x50), so exactly `lab: pins 38 39`
  (`v07-lab-boot`). Nothing is driven.
* **Emulator, test image:** `stark_lab_transport` is substituted by the pad model (the fifth and
  last allowed substitution, checked by `scripts/check_emu_image.py`); the offer follows slot 0
  through production code and a pulled module logs each withdrawn pin (`storage/v07-pins`).
* **Probes** (docs/VALIDATION.md §2): the output pad reads back and a self-driven pad raises its
  GPIO interrupt on esp-emu 0.44.0; pull-down reads and LEDC on a pad do not work there, so the
  tests that need them run on the host and the test image only.
* **Not proven anywhere before V1:** that the offered pins are the only reachable ones, drive
  strength, hi-Z on the board, the current into or out of a pin, 3.3 V tolerance.

**What runs where (STARK-0702).**
* **Host:** the PWM and pulse rules and the LEDC arithmetic swept over every frequency from 100 to
  40 000 Hz (`test_lab_pwm`: divider in range, error at most 0.2 %, 997 ppm worst); the pure core's new
  calls (`test_lab_pins`: toggle, info, hi-Z, one PWM at a time, every exit path stopping it, a pulse
  restoring the pad's mode, contention and faults ending in hi-Z, the sweep of every number against
  every call); the transport over the fake HAL (`test_lab_service`: the sweep again for PWM and
  pulse, LEDC channel 2 only with the buzzer's and backlight's untouched, pulse widths and the
  cancel); the pure pin view (`test_gpio_views`); and the real app on the UI port over the real pad
  decisions and the pad model (`test_ui_gpio`: the home list, each mode, the confirmation with BACK
  touching nothing, the contention alert, the hot-plug withdrawing a driven slot pin, one pulse per
  press, every exit path leaving every pad in hi-Z; 20 frames pinned after inspection).
  `lab_pins.c`, `lab_pwm.c`, `lab_transport_hal.c`, `fake_lab.c` and `gpio_views.c` run at 100 % lines.
* **Emulator, production image:** `v07-gpio-prod` lists exactly pins 38 and 39, sets 38 to an input
  with a pull-up and reads it high (the emulator reads every unconnected input high, so this does
  not exercise a pull), opens no dialog, and the exit logs `lab: released all`.
* **Emulator, test image:** `storage/v07-gpio` (the confirmation, BACK on it, the toggle, a short
  to ground as a contention, a slot pin withdrawn by a module) and `storage/v07-gpio-pwm` (the
  PWM request with a wire from pad 38 to 39 checked by the pad model, a pulse, a pad-model fault).
  LEDC does not reach a pad in esp-emu, so no PWM waveform is claimed here.
* **Not proven anywhere before V1:** the PWM waveform on a logic analyser, a pulse's width, a
  short to the idle level during a pulse, a PWM against a driven pad, drive strength.

**What runs where (STARK-0703).**
* **Host:** the arithmetic (`test_lab_meas`: 79 edge sets from `scripts/gen_lab_vectors.py`, whose
  independent Python reference computes every expected result exactly; 49 of them are also held to the
  accuracy of ADR-0023 §4, ±0.5 % to 10 kHz and ±2 % to 50 kHz, ±2 and ±5 points of duty, with seeded
  jitter of up to ±2 µs; every status, a counter wrap, hostile input and 10 000 seeded random edge sets
  that never return a number outside their status); the window plan for every gate count; the service
  over the pad model (`test_lab_pins`: gate, window and ring bounds, a PWM wired to a measured pad at
  six frequency and duty pairs, a sweep of 40 frequency and duty pairs with jitter, too fast, steady
  lines, overflow, glitches, a cancel and a failure); the transport over the fake HAL
  (`test_lab_service`: every number from -1 to 64 against the capture, the interrupt on for the window
  only); the pad model's own edge synthesis (`test_fake_lab`); and the Measure row on the UI port
  (`test_ui_gpio`, frames pinned after inspection). `lab_meas.c`, `lab_pins.c`,
  `lab_transport_hal.c`, `fake_lab.c` and `gpio_views.c` run at 100 % lines.
* **Emulator, production image:** `v07-measure-prod` opens pin 38 as an input with a pull-up and
  measures it. The real GPIO interrupt service (installed with the IRAM flag), the IRAM edge handler,
  the cycle counter and the window wait run; nothing drives the pad, so no edge comes and the answer is
  `no signal (high)` after the 4 s window, with the interrupt removed again. That is all the emulator
  can show: it cannot stimulate a pad from outside, and a pad the firmware drives itself is not
  something the GPIO app measures.
* **Emulator, test image:** `storage/v07-measure` measures an external source on pad 39 through the app
  at 1 kHz / 25 %, 40 kHz / 50 %, 80 kHz (`too fast` from the gate) and a steady high line, and cuts one
  measurement short with BACK. The stamps are the pad model's, from the emulated clock.
* **Not proven anywhere before V1:** the interrupt latency, the accuracy of a real signal from 1 Hz to
  50 kHz (a signal generator, ROADMAP V1 exit 13), whether a flash operation during a window disturbs a stamp
  (the handler is in IRAM so it should not), and a PWM wired into another pad: the GPIO app holds one
  pin at a time, so that check needs a signal generator or another tool (ADR-0023, 0703 addendum 10).

**What runs where (STARK-0704).**
* **Host:** the sweep's rules (`test_lab_i2c`: 15 sweeps from `scripts/gen_lab_vectors.py` whose independent
  Python reference computes the list, the error count, the status and the number of probes: none, one, all
  112, the edges, one and two timeouts, a NACK or an ACK between errors, three in a row at the start, the
  middle and the end, every address timing out); the refusal table (112 vectors, the core and the transport's
  guard agree with the reference on every one); the 32-bit FNV-1a fingerprints (16 vectors, all distinct) and
  the hex text with its whole-bytes truncation (110 vectors); the bus model's five devices and faults
  (`test_lab_i2c`, `test_fake_lab`: WHO_AM_I, a write that reads back, a write to a read-only register that
  does not, auto-increment and wrap, a device that vanishes, a timeout at one address, a stuck bus, a NACK
  storm bounded at 112 probes); the arm (consumed, a different payload, stale, replaced, cleared, the clock
  wrapping); every length and address edge; no bus traffic for a refused write; the hints table (31 entries
  sorted, "may be", at most 30 characters, a lookup that probes nothing); the app's rows (`test_i2c_views`: every
  byte reachable by nibbles) and the app on the UI port (`test_ui_i2c`: scan, no devices, bus error and
  recovery, a crowded bus, read by nibbles, 16-bit registers, a vanished device, a dump and a write behind
  their confirmations, the refusals, an arm that dies with the screen; 33 frames pinned after inspection).
  `lab_i2c.c`, `lab_i2c_hints.c`, `i2c_views.c` and `fake_lab.c` run at 100 % lines.
* **Emulator, production image:** `v07-i2c-prod` (the row above): the real driver and transport find 0x50
  and nothing else, read zeros from it, and the service refuses a write to it.
* **Emulator, test image:** `storage/v07-i2c` (the five devices, WHO_AM_I `0x68`, a cancelled then a confirmed
  write of register 0x6b that reads back) and `storage/v07-i2c-bus` (the module's 24C02 and the fake PN532 on
  the same bus as the five, the refusals of both, a dump, a stuck bus and its recovery). The bus is the pad
  model's; the EEPROM and the PN532 answer through their own fakes.
* **Not proven anywhere before V1:** a real device at its address, pull-ups, the 100 and 400 kHz waveforms,
  bus recovery after a device is pulled mid-transaction, and the lab's temporary device handle sharing the
  bus with the NFC and module handles at the real clocks (ADR-0023, 0704 addendum 2).

**What runs where (STARK-0705).**
* **Host:** the data rules (`test_lab_uart`: wire times and the loopback window, the 4 kB ring and its loss count over 14
  sequences, the judgement of 21 loopbacks, 29 hex and ASCII rows, 41 send files against the strict grammar, the log
  names, zlib's CRC-32, and the fake peer's two scenes against the CRC-32 the generator computes), auto-baud
  (`test_lab_autobaud`: 72 edge sets from `scripts/gen_lab_vectors.py` whose independent Python estimator names the baud
  at every baud and format, with jitter, runs of identical bits, idle gaps and spikes, the 3 % snap from both sides, bauds
  that are not on the list, too few edges, glitches, a missing clock and stamps that go backwards; and the fake peer's own
  stream read back at every baud and format), the session over the real pin hold, the host shim of the production guard
  and the peer (`test_lab_uart_svc`: both pins held while open and free after every exit, TX off until armed, a new
  baud or format, the ring and its counters, errors and a buffer overrun counted and announced at most ten times a second,
  the loopback at every baud and format and against a matching, a garbling, a slow, a mute and a talking peer, every
  transport failure ending the session with the driver removed, auto-baud holding the RX pin only for the capture), the
  app's rows (`test_uart_views`), the card log over an in-memory `stark_storage` (`test_uart_log`: numbering, the 2 kB buffer,
  the sync every 4 kB, 1 MiB exactly, a card that goes away, every card error) and the app on the UI port
  (`test_ui_uart`: 37 frames pinned after inspection). `lab_uart.c`, `lab_autobaud.c`, `lab_uart_svc.c`, `uart_views.c`,
  `uart_log.c` and `fake_lab.c` run at 100 % lines; `scripts/test_emu_harness.py` covers the `uart1_peer` thread.
* **Emulator, production image:** `v07-uart-prod` (the row of §2 above): the real UART driver and transport carry the
  loopback pattern through a host peer; no baud, level or error is shown.
* **Emulator, test image:** `storage/v07-uart` (loopback 64/64, an echo peer at the wrong baud reported as a mismatch,
  auto-baud 38 400 applied to the Baud row, the monitor, a send behind its confirmation), `v07-uart-bauds` (every baud of
  the list), `v07-uart-formats` (every format) and `v07-uart-log` (the scene logged, its CRC-32 equal to the generator's,
  a second file, the GPIO app then opening the released pin 38). The peer is the pad model's, spec-derived.
* **Not proven anywhere before V1:** a baud on a wire (the S3 model has no timing, the pad model is arithmetic), framing and
  parity errors from a real line, levels and the 3.3 V limit, an adapter on the other end, the highest baud the edge
  capture can follow, the glitch when a format change routes TX again, a long log on a real card, and the interrupt
  handling at 921 600 baud with the driver's 2 kB buffer while the card or a flash write holds the CPU (ADR-0023, 0705
  addendum).

**What runs where (STARK-0706).**
* **Host:** the probe's rules (`test_lab_spi`, over the real module core and the fake 24C02, the host shim of the transport's
  SPI group and the test-only SPI devices): which slot may be probed and why not (220 vectors from `scripts/gen_lab_vectors.py`
  whose independent Python reference decides every state against 22 declared-bit mixes, with and without the chip select, and
  the transport's own predicate agreeing with it on every one), the request's fingerprint (14 vectors, all distinct), every
  clock, mode and length at its edges, a request consumed by success, failure, a bad argument and a cancel, stale at 30 s and
  not before (the clock's wrap included), any changed field or a different module in the slot, a module swapped for a radio or
  pulled before the send and between the claim and the transfer, a stale lease, a busy slot left alone, and the lease released
  after every outcome; and that **no path reaches a driver-owned slot**: with the CC1101 module in slot 0 and the fake chip bound
  behind the SPI model, a PATABLE burst, an `STX` strobe and 32 bytes of `ff` are refused at the request, at a hand-made send and
  at the transport with a lease the radio's own driver could hold, and the chip's transfer count, PATABLE writes, strobes and
  MARCSTATE do not move, while the same harness on an eligible slot does reach the chip (the control). The devices'
  answers (`test_fake_lab`: none, JEDEC flash, echo, `SPI_ERROR`, `NO_ANSWER`, a bound chip) match a second independent Python
  model (54 vectors); the app's rows (`test_spi_views`) and the real app on the UI port (`test_ui_spi`: both slots with their
  reason, a driver-owned slot refused with an alert and no traffic, a JEDEC read behind a confirmation whose BACK sends nothing
  and whose second press asks again, the presets, the clock, mode and byte editors, a 32-byte answer in four hex and four text
  rows, a module swapped for a radio or pulled after the dialog opened, a stale request, a busy slot, a failing bus, a request
  that never outlives the app; 16 frames pinned after inspection). `lab_spi.c`, `spi_views.c` and `fake_lab.c` run at 100 % lines.
* **Emulator, production image:** `v07-spi-prod`: esp-emu answers at 0x50, so slot 0 reads as a generic module and slot 1 has no
  chip select; the real module table and the real guard refuse both with an alert, no lease is claimed, no transfer is made.
  No transaction is attempted on this image: its only slot with a chip select reads as a generic module and is refused (nothing
  was probed on esp-emu's GP-SPI for this task, so no claim is made about what it would answer).
* **Emulator, test image:** `storage/v07-spi` (catalogue entry 34 and a JEDEC flash: BACK sends nothing, OK makes one transaction
  with the claim and release visible in the log, a second press asks again, an echo device, then the module swapped for the
  CC1101 module: `owned by Sub-GHz`, an alert, never claimed by the probe, the Sub-GHz detection unchanged; then an empty slot).
  The device is the pad model's, spec-derived.
* **Not proven anywhere before V1:** a transaction on MOSI, MISO and SCK at 100 kHz to 4 MHz in each mode, the panel and the card
  staying intact while the probe uses the shared bus (no display corruption, a logic-analyser capture of the chip selects), a real
  flash's JEDEC ID, the refusal on a real Sub-GHz module, and how a breakout's own pull-ups and levels behave (ADR-0023, 0706
  addendum).

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
