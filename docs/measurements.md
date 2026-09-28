# Measurements

Two kinds of figures live here and must never be confused (ADR-0011,
TESTING.md §1.1):

* **Simulated (informational)** — produced in simulation: Wokwi for V0, Espressif's
  free esp-emulator from V0.1 on (ADR-0017). Timing figures are trend data only and
  are **never** a pass/fail gate; memory and counters are deterministic and *are*
  simulation gates (heap, leak deltas, dropped events).
* **Hardware (gated, V1 prototype)** — measured on the physical breadboard. This is
  where every performance gate lives. Empty until V1.

## Simulated (informational)

Environment: Wokwi, `board-esp32-s3-devkitc-1` configured as the N16R8 SKU (16 MB
flash, 8 MB octal PSRAM declared, `CONFIG_SPIRAM` off — ADR-0014), firmware built
with `espressif/idf:v6.1` from a clean `sdkconfig`, SPI 40 MHz, band height 40.

| Date | Task | Metric | Value | Source |
| --- | --- | --- | --- | --- |
| 2026-09-27 | STARK-0021 | Boot to root menu (`boot: ui_ready in <N> ms`) | 568 ms | `v0-boot` serial log |
| 2026-09-27 | STARK-0016 | Full-screen refresh (`display: full refresh <N> ms`) | 119–159 ms (132 ms first recorded) | serial logs across runs |
| 2026-09-27 | STARK-0020 | Display Test continuous full-area refresh (`displaytest: fps=<N>`) | 5.5 fps | Display Test |
| 2026-09-27 | STARK-0021 | Application binary (`stark-one.bin`) | 253 120 B (0x3dcc0; 94 % of the 4 MB app partition free) | `idf.py build` |
| 2026-09-27 | STARK-0021 | Free internal heap at the root menu (`diag: heap=<n>`) — **gate ≥ 200 kB** | 315 352 B | `v0-boot`, checked by `scripts/test_wokwi.sh` |
| 2026-09-27 | STARK-0020 | Free heap after 10 launch/exit cycles of each app — **gate within 1 kB** | 319 556 → 319 556 B (Δ 0) | About screen, leak scenario |
| 2026-09-27 | STARK-0021 | Dropped events / render errors over the full scenario set — **gate 0** | 0 / 0 (peak queue depth 1) | throwaway stats build, all three scenarios |

Wokwi's SPI timing is not the panel's, so the simulated refresh time and FPS say
nothing about the hardware targets; they are recorded to spot regressions in the
firmware's own per-band work between runs.

### V0.1 (esp-emulator 0.44.0, production merged image)

Environment: esp-emu 0.44.0 (pinned, `scripts/esp_emu.lock`), ESP32-S3 dual core,
16 MB flash from the image header, PSRAM unused (`CONFIG_SPIRAM` off); firmware built
with `espressif/idf:v6.1` from a clean `sdkconfig`. Host figures: `scripts/test_host.sh
--coverage` (clang 18, ASan/UBSan).

| Date | Task | Metric | Value | Source |
| --- | --- | --- | --- | --- |
| 2026-09-28 | STARK-0110 | Free internal heap at boot (`diag: heap=<n>`) — **gate ≥ 200 kB** | 308 176 B | every emulator scenario |
| 2026-09-28 | STARK-0110 | Root-menu heap, first vs last stark_diag line over the 60 s soak — **gate ≤ 1 kB** | 312 316 → 312 352 B (Δ 36 B) | `soak/v01-soak` |
| 2026-09-28 | STARK-0110 | Event drops over the soak and the full scenario set — **gate 0** | 0 (every `diag:` line) | `test_emu.py` drops gate |
| 2026-09-28 | STARK-0110 | Frame overruns over the soak (reported, **not** gated — ADR-0011) | 2 | `soak/v01-soak` `diag:` lines |
| 2026-09-28 | STARK-0110 | UI FPS in the emulator (informational; the UI renders on demand) | 0.0–7.0 per 1 s window | `diag:` lines |
| 2026-09-28 | STARK-0110 | Application binary (`stark-one.bin`) | 272 000 B (0x42680; V0: 253 120 B) | `idf.py build` |
| 2026-09-28 | STARK-0102 | Flash cost of Latin-1/Turkish coverage + the 6×10 font | +7 776 B | `idf.py build` |
| 2026-09-28 | STARK-0108 | Boot heap cost of the static app worker slot | −4 368 B | emulator boot heap |
| 2026-09-28 | STARK-0108 | Heap after 20 worker start/stop cycles (About's reading) — **gate unchanged** | 314 312 → 314 312 B (Δ 0) | throwaway leak run |
| 2026-09-28 | STARK-0110 | Host line coverage, pure sources | 621 / 625 (99.4 %); every V0.1 pure helper 100 % | `test_host.sh --coverage` |

### V0.1 exit criteria — evidence (ROADMAP.md "V0.1")

| # | Criterion | Evidence |
| --- | --- | --- |
| 1 | A "hello" app in one new directory, < 120 lines, no edits to `components/` | Commit `a73196f` (STARK-0104) touched only `apps/app_hello/**` (96 lines) and one line each in `main/app_registry.{h,c}` (ADR-0016); `git grep app_hello -- components` is empty; 105 lines today with its icon |
| 2 | Modal confirm dialog works, covered by a scenario | `test/emu/v01-dialog.toml` on the production image; `test_ui_dialog.c` on the host UI port (overlay pixels, held-key rule, copied strings) |
| 3 | Event drops zero over a 60 s scenario; overruns reported, gated only on hardware | `test/emu/soak/v01-soak.toml`: 60 s emulated, 16 app visits (dialogs, a worker, a contained fault, long-BACK), `drops=0` on every `diag:` line, root heap Δ 36 B; 2 overruns reported, not asserted |
| 4 | Diagnostics app displays live heap and FPS | `test/emu/v01-diag.toml` (live refresh on the production image); `test_ui_diagnostics.c` (rows pixel-identical to the values) |

## Hardware (gated, V1 prototype)

To be filled in at the V1 prototype milestone (ROADMAP.md "V1 prototype"), with the
simulated figure above kept alongside for reference.

| Metric | Target | Measured | Date |
| --- | --- | --- | --- |
| `boot: ui_ready` from reset | ≤ 600 ms | | |
| First pixel from reset | ≤ 400 ms | | |
| Root menu frame rate | ≥ 20 FPS | | |
| Full-screen refresh at the chosen SPI clock | ≤ 60 ms (~25 ms SPI time at 40 MHz, ADR-0004) | | |
| Key-to-pixel latency, selection move | ≤ 50 ms | | |
| Frame overruns over a 60 s navigation session | 0 | | |
| Free internal heap at the root menu | ≥ 200 kB | | |
| Binary size | (record) | | |
| Current draw, idle / backlight on | (record) | | |

## V0 exit criteria — evidence (ROADMAP.md "V0")

| # | Criterion | Evidence |
| --- | --- | --- |
| 1 | `idf.py build` clean, warnings-as-errors, pinned toolchain in CI | CI `firmware` job (`espressif/idf:v6.1`), 0 warnings on every merge STARK-0008…0021 |
| 2 | `v0-boot-and-menu` passes: boot → navigate → launch app → BACK | `test/scenarios/v0-boot-and-menu.yaml` via `scripts/test_wokwi.sh`, CI `wokwi` job |
| 3 | Host suite green, ≥ 80 % lines over the four pure cores | `scripts/test_host.sh --coverage`: `stark_event_core` 100 %, `stark_gfx` 100 %, `input_core` 100 %, `ui_menu_model` 97 % |
| 4 | `boot: ui_ready in <N> ms` logged in order, value informational | `v0-boot` asserts it after `display: init` and `app: registry`; 568 ms recorded above, not asserted |
| 5 | Rendering correct: no seams, partial = requested region, full ≡ partial | STARK-0016: screenshots decoded against an exact pattern model (0 pixels differ), a 50×30 partial render changed exactly 1 500 pixels, 7 partial renders ≡ one full render; FPS logged only |
| 6 | ≥ 200 kB heap at the root menu; 10× launch/exit each app within 1 kB | `diag: heap=315352` (gated by `scripts/test_wokwi.sh`); leak run Δ 0 B (STARK-0020) |
| 7 | Zero dropped events and zero render errors over the scenario set | stats build over all three scenarios: dropped 0, render failures 0 |
| 8 | No `#ifdef` referencing the simulator | `scripts/check.sh` `no-wokwi-ifdef` and `no-wokwi-mention` (`grep -ri wokwi components/ apps/ main/` empty) |
