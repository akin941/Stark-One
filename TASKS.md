# TASKS — STARK ONE implementation backlog

**How to use this file.** Work top to bottom. Take exactly one task, satisfy every
acceptance criterion, stop, and let it be reviewed. Do **not** batch tasks. Do **not**
implement anything a later task owns — an unused abstraction created early is a defect.

Every task is written to be completable in one focused session. If a task turns out to
be bigger than that, stop and split it rather than growing it.

**Legend:** `Deps` = task IDs that must be complete first. `AC` = acceptance criteria,
all of which must hold. Definition of done also requires [PROJECT.md §9](PROJECT.md).

---

# V0 — Wokwi core prototype

## STARK-0001 — Repository & toolchain bootstrap

**Goal.** A reproducible, empty-but-correct ESP-IDF project skeleton with pinned
toolchain and working scripts. No firmware logic.

**Files.** `CMakeLists.txt`, `sdkconfig.defaults`, `sdkconfig.defaults.esp32s3`,
`partitions.csv`, `.idf-version`, `.gitignore` (exists — extend if needed),
`scripts/{setup.sh,build.sh,fmt.sh,check.sh}`, `docs/DEVELOPMENT.md`.

**Deps.** none.

**Notes.**
* Verify the exact current ESP-IDF v6.1 patch tag at `https://github.com/espressif/esp-idf/releases` before pinning; record what you pinned and the date in `docs/DEVELOPMENT.md`. If v6.1 has a patch release (v6.1.x), pin that. Do not silently pin something else — if v6.1 is unavailable for any reason, use v6.0.3 and note the deviation (ADR-0003 designates it the fallback).
* `sdkconfig.defaults` must set at minimum: `CONFIG_IDF_TARGET="esp32s3"`, `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`, `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` (ADR-0015), `CONFIG_FREERTOS_HZ=1000`, `CONFIG_LOG_DEFAULT_LEVEL_INFO=y`, `CONFIG_PARTITION_TABLE_CUSTOM=y`, and PSRAM left disabled (ADR-0014).
* `partitions.csv` for V0: `nvs 24K`, `phy_init 4K`, `factory 4M`. Leave space for a later storage/OTA layout; do not add partitions V0 does not use.
* Project name `stark-one` so the ELF is `build/stark-one.elf` (WOKWI.md §3 depends on it).
* `scripts/setup.sh` asserts the IDF version and exits non-zero on mismatch.
* `scripts/build.sh` supports `--docker` to run the pinned image, so a machine without a local IDF can still build.

**Host tests.** none.

**Wokwi/manual.** `scripts/build.sh` succeeds locally and via `--docker`.

**AC.**
1. `idf.py build` succeeds from a clean tree with zero warnings.
2. `docker run --rm -v $PWD:/p -w /p espressif/idf:<pinned> idf.py build` succeeds and produces an identical `build/stark-one.elf` size (±0 bytes is not required; the build must simply succeed).
3. `.idf-version` contains the pinned tag and `scripts/setup.sh` fails loudly on a mismatched environment.
4. `git status` is clean after a build (no generated files tracked).
5. `docs/DEVELOPMENT.md` documents: install, build, flash, monitor, Docker path, and the pinned versions with the date they were verified.

---

## STARK-0002 — Minimal boot & `stark_log`

**Goal.** The device boots, prints identifiable banner lines, and has a logging facade
the rest of the firmware uses instead of `esp_log` directly.

**Files.** `main/{CMakeLists.txt,stark_main.c}`, `components/stark_err/**`,
`components/stark_log/**`.

**Deps.** STARK-0001.

**Notes.**
* `stark_err.h` per ARCHITECTURE §5 — header-only, no dependencies, plus `stark_err_str()` in a `.c`.
* `stark_log` wraps `esp_log` with `STARK_LOGI/W/E/D(tag, fmt, ...)`. It exists so a file sink can be added at V0.2 without touching call sites — do not add the sink now.
* `app_main` logs a banner: firmware name, version (from git describe via CMake, falling back to `dev`), build timestamp, IDF version, chip revision, free heap.
* Log tags are per-component, uppercase-free snake: `"board"`, `"ui"`, `"input"`.

**Host tests.** `stark_err_str()` returns a non-NULL distinct string for every enum
value (a compile-time-complete switch, no `default:` — so adding an error code breaks
the build until it is named).

**Wokwi/manual.** Not yet (no diagram until STARK-0003); verify with `idf.py monitor`
or defer observation to STARK-0003.

**AC.**
1. Boot prints `boot: stark-one <version> idf=<ver> heap=<n>`.
2. No direct `ESP_LOG*` call exists outside `components/stark_log`.
3. Build clean; `stark_err` has no include dependencies at all.

---

## STARK-0003 — Wokwi baseline: `diagram.json` + `wokwi.toml`

**Goal.** The firmware from STARK-0002 runs in Wokwi and its serial output is visible.

**Files.** `diagram.json`, `wokwi.toml`, `docs/DEVELOPMENT.md` (Wokwi section).

**Deps.** STARK-0002.

**Notes.**
* Start with the **board only** plus the status LED — do not model the panel or buttons yet; each is introduced by the task that brings it up. This keeps the diagram diffable and each failure attributable.
* `wokwi.toml` exactly as in [WOKWI.md §3](WOKWI.md).
* **Board attrs must model the physical SKU** (ESP32-S3-WROOM-1-N16R8):
  `attrs: { "flashSize": "16", "psramSize": "8", "psramType": "octal" }`.
  Firmware keeps `CONFIG_SPIRAM` **off** (ADR-0014) — the point is to prove V0 runs on
  the production SKU without using PSRAM.
* `flashSize` and `psramSize` are documented Wokwi attributes. **`psramType` is not
  confirmed in Wokwi's public docs** — set it, then verify the simulator accepts the
  diagram without warnings and that boot is unaffected. Record the result in WOKWI.md §4:
  if it is ignored or rejected, remove it, keep `psramSize: "8"`, and state plainly that
  the simulator models PSRAM size but not interface mode. Do not leave it unverified.
* Confirm the real Wokwi pin-label strings for `board-esp32-s3-devkitc-1` while authoring, and write them down in WOKWI.md §4 if they differ from what is documented there.

**Host tests.** none.

**Wokwi/manual.** VS Code extension: simulator starts, serial shows the STARK-0002
banner within 2 s.

**AC.**
1. Simulator boots the built firmware with no manual file selection.
2. The banner line appears in the Wokwi serial monitor.
3. `diagram.json` is 2-space formatted, contains only the board (+ LED once STARK-0008 lands), and is committed alongside `wokwi.toml`.
4. Board attrs declare 16 MB flash and 8 MB PSRAM; the `psramType` question is resolved one way or the other and the answer is written into WOKWI.md §4.
5. Boot succeeds with `CONFIG_SPIRAM` disabled on the PSRAM-equipped simulated SKU, and the banner's reported flash size is 16 MB.

---

## STARK-0004 — CI pipeline (build + format + secrets)

**Goal.** Every push is built and linted identically to a developer's machine.

**Files.** `.github/workflows/ci.yml`, `.clang-format` (exists — verify),
`scripts/{fmt.sh,check.sh}`.

**Deps.** STARK-0002.

**Notes.**
* Two jobs for now: `lint` (clang-format check + gitleaks) and `firmware` (build in `espressif/idf:<pinned>` + `idf.py size` artifact). Host tests and Wokwi jobs are added by STARK-0005 and STARK-0021 respectively — do not stub them.
* `fmt.sh` formats `main/`, `components/`, `apps/`, `test/`; `--check` mode returns non-zero on any diff.
* Fail the build if `dependencies.lock` changes during CI.

**Host tests.** none.

**Wokwi/manual.** Push a branch; both jobs must pass.

**AC.**
1. CI green on a clean tree.
2. Deliberately misformatting a file fails `lint` (verify once, then revert).
3. A deliberately added dummy secret string fails `gitleaks` (verify once, then revert).
4. Build job uses the pinned image tag, not `latest`.

---

## STARK-0005 — Host test harness

**Goal.** A fast, sanitised host unit-test project with one real test, wired into CI.

**Files.** `test/host/**`, `scripts/test_host.sh`, `.github/workflows/ci.yml` (add the
`host` job).

**Deps.** STARK-0002.

**Notes.**
* Standalone CMake project per [TESTING.md §2](TESTING.md); Unity pinned by exact tag.
* `-Wall -Wextra -Werror -fsanitize=address,undefined`, C11.
* `test/host/support/stark_hal_host.c` provides the fake clock and fake GPIO array now — it is used from STARK-0011 onwards, but the harness needs a shape to build against. Keep it to the four functions ARCHITECTURE §6.2 lists; nothing speculative.
* The one real test: `stark_err_str()` completeness from STARK-0002.
* Add `gcovr` coverage reporting as an artifact (not a gate yet).

**Host tests.** The harness itself.

**Wokwi/manual.** `scripts/test_host.sh` runs green on macOS and in CI on Linux.

**AC.**
1. `scripts/test_host.sh` builds and runs in < 10 s from cold, < 2 s incremental.
2. The host project compiles with **no ESP-IDF include paths** — verify by grepping the generated compile commands for `esp-idf`.
3. CI `host` job passes on both `ubuntu-latest` and `macos-latest`.
4. A deliberately failing assert fails the job (verify once, then revert).

---

## STARK-0006 — `stark_board`: pin map & board init

**Goal.** One authoritative place for wiring; nothing else in the firmware names a GPIO.

**Files.** `components/stark_board/**` (`include/stark_board.h`, `stark_board.c`,
`board_devkitc1.c`, `Kconfig`), `scripts/check_pins.py`.

**Deps.** STARK-0002.

**Notes.**
* Structures exactly as ARCHITECTURE §6.1; pin values exactly as [HARDWARE.md §2](HARDWARE.md).
* `stark_board_init()` in this task does: configure the status LED as output, configure the six key GPIOs as inputs with pull-ups, and initialise the SPI2 bus (`spi_bus_initialize`, max transfer = `320 * BAND_H * 2`). It does **not** touch the panel.
* Kconfig: `STARK_BOARD_DEVKITC1` (default). Add the `PCB_R1` symbol **only** when that board exists — no empty variants.
* `scripts/check_pins.py` parses `board_devkitc1.c` and `diagram.json` and asserts they agree; it must tolerate signals not yet present in the diagram (report, do not fail) but must fail on a *mismatch*.
* Assert at init that no two assigned pins collide, and that no assigned pin falls in 26–37, 19–20, 43–46 (log and `stark_panic()` — a wiring mistake must not be subtle).

**Host tests.** Pin-collision and reserved-range validation logic extracted to a pure
function `stark_board_validate(const stark_board_pins_t*)` and tested with a good map,
a colliding map, and a reserved-pin map.

**Wokwi/manual.** Boot logs `board: devkitc1 pins ok spi2 ready`.

**AC.**
1. No GPIO number appears anywhere outside `components/stark_board` (grep-verifiable).
2. `check_pins.py` passes and is wired into the `lint` CI job.
3. `stark_board_validate()` rejects the two bad maps in host tests.
4. Boot log line present.

---

## STARK-0007 — `stark_hal`: time, GPIO, PWM port

**Goal.** The thin port layer that lets cores be host-testable.

**Files.** `components/stark_hal/**` (`include/stark_hal.h`, `stark_hal_esp.c`),
`test/host/support/stark_hal_host.c` (implement against the same header).

**Deps.** STARK-0006, STARK-0005.

**Notes.**
* Exactly the API in ARCHITECTURE §6.2 — time, GPIO, PWM. **No SPI wrapper.** Resist the urge to abstract more; anything not consumed by a V0 task must not exist.
* `stark_hal_now_us()` uses `esp_timer_get_time()` on target and a settable counter on host.
* PWM uses LEDC; channel allocation is caller-specified (buzzer = channel 0, backlight = channel 1) and documented in the header.
* `stark_err_from_esp()` lives here.

**Host tests.** Fake clock advances only when told; fake GPIO read returns the last
written value; PWM calls record their arguments for inspection.

**Wokwi/manual.** Covered by STARK-0008.

**AC.**
1. Both implementations compile against the identical header, with no `#ifdef` in the header.
2. Host implementation is used by at least one passing test.
3. Header documents the LEDC channel allocation.

---

## STARK-0008 — Status LED heartbeat

**Goal.** First visible sign of life; validates board init + HAL + timers end to end.

**Files.** `components/stark_board/stark_board.c` (or a tiny `stark_led.c` inside it),
`main/stark_main.c`, `diagram.json` (add LED + 330 Ω resistor on GPIO 18).

**Deps.** STARK-0007, STARK-0003.

**Notes.**
* `esp_timer` periodic callback, 1 Hz, 10 % duty (short blink) — a distinctive pattern so a stuck LED is obvious.
* Keep it in `stark_board`; it is board plumbing, not a service worth its own component yet.

**Host tests.** none (pure plumbing).

**Wokwi/manual.** LED blinks in the simulator at 1 Hz.

**AC.**
1. LED visibly blinks in Wokwi.
2. `expect-pin` on GPIO 18 can observe both states (verified manually now; scripted in STARK-0021).
3. `diagram.json` updated and `check_pins.py` still passes.

---

## STARK-0009 — `stark_event` core (pure)

**Goal.** The event bus data structure, fully host-tested, with no FreeRTOS in sight.

**Files.** `components/stark_event/{include/stark_event.h,stark_event_core.c,
include/stark_event_core.h}`, `test/host/test_event.c`.

**Deps.** STARK-0005.

**Notes.**
* Types and API exactly as ARCHITECTURE §6.3. Fixed capacity, caller-provided storage, injected lock (a no-op lock on host).
* Overflow drops the **oldest** and increments `dropped`.
* Subscriber table: max 8, each with a type mask; `dispatch()` returns how many events it delivered and never recurses.
* Publishing from within a handler must be safe (it lands in the ring and is delivered on the next dispatch, not this one) — test it.

**Host tests.** Everything in [TESTING.md §2](TESTING.md) under *Event bus*, plus
publish-from-handler.

**Wokwi/manual.** none.

**AC.**
1. All listed host cases pass under ASan/UBSan.
2. `stark_event_core.c` includes nothing but `<stdint.h>`, `<stdbool.h>`, `<string.h>` and its own headers.
3. Coverage over the core ≥ 90 %.

---

## STARK-0010 — `stark_event` port (FreeRTOS binding)

**Goal.** One global bus usable from tasks, with a blocking wait for the UI loop.

**Files.** `components/stark_event/stark_event.c`, `Kconfig`.

**Deps.** STARK-0009, STARK-0007.

**Notes.**
* Global bus with static storage sized by `STARK_EVENT_QUEUE_LEN` (default 32).
* Mutex as the injected lock; a binary/counting semaphore signalled on publish so
  `stark_event_wait(timeout_ms)` can block the UI task.
* `stark_event_publish()` stamps `ts_us` from `stark_hal_now_us()` if the caller left it zero.
* **Do not** add an ISR-safe publish path in V0 — no producer needs it (input uses an `esp_timer` callback, which is a task context).

**Host tests.** none (the port is thin by construction).

**Wokwi/manual.** A temporary throwaway check is acceptable during development but must
not be committed; real coverage arrives with STARK-0012.

**AC.**
1. `stark_event_wait()` returns promptly on publish and honours its timeout.
2. No allocation after `stark_event_init()`.
3. `stark_event_stats()` exposes `published`/`dropped`/`max_depth`.

---

## STARK-0011 — `stark_input` core: debounce & repeat FSM (pure)

**Goal.** Deterministic key semantics, proven on the host before any GPIO is involved.

**Files.** `components/stark_input/{include/stark_input.h,input_core.c,
include/input_core.h}`, `test/host/test_input_core.c`.

**Deps.** STARK-0009.

**Notes.**
* Signature: `size_t input_core_update(input_core_t *c, uint8_t raw_bitmap, uint32_t now_ms, input_action_t *out, size_t max_out)`.
* Timings per ARCHITECTURE §6.6: 20 ms debounce, 500 ms long-press, 400 ms repeat delay, 120 ms repeat interval. Put them in named constants, not literals.
* `REPEAT` only for UP/DOWN/LEFT/RIGHT. `LONG` is emitted once while held; `SHORT` on release only if `LONG` was not emitted.
* Keys are independent; no chord handling in V0 (leave no hook for it either).

**Host tests.** The full *Input FSM* list in [TESTING.md §2](TESTING.md), driven by an
explicit `now_ms` timeline — no sleeping.

**Wokwi/manual.** none.

**AC.**
1. All FSM cases pass; coverage ≥ 90 %.
2. Timings are named constants and are exercised by boundary tests (19 ms vs 21 ms; 499 ms vs 501 ms).
3. The core has no ESP-IDF or FreeRTOS includes.

---

## STARK-0012 — `stark_input` service: sampling & events

**Goal.** Real buttons produce real events on the bus.

**Files.** `components/stark_input/input_service.c`, `diagram.json` (add six buttons),
`main/stark_main.c`.

**Deps.** STARK-0011, STARK-0010, STARK-0008.

**Notes.**
* 5 ms periodic `esp_timer` reads the six pins via `stark_hal_gpio_read()` (active-low → invert into the bitmap), runs the core, publishes `STARK_EVT_KEY`.
* The callback must be short and must never block; if the bus is full, the drop counter is the signal — do not retry in the callback.
* Log `key: <NAME> <action>` at DEBUG for scenario use; keep the wording stable (TESTING.md §4).
* Diagram: six `wokwi-pushbutton` parts arranged as a D-pad diamond + OK centre + BACK right, `bounce: 1`.

**Host tests.** none beyond STARK-0011.

**Wokwi/manual.** Press each button in the simulator; the matching `key:` line appears
exactly once per press, once per release.

**AC.**
1. All six keys are distinguishable and correctly named in the log.
2. Holding a direction key produces repeats at the configured cadence; OK/BACK do not repeat.
3. `diagram.json` matches the pin map (`check_pins.py` green).
4. No dropped events over a 30 s button-mashing session (`stark_event_stats()` reports 0).

---

## STARK-0013 — `stark_gfx` core: surfaces & primitives (pure)

**Goal.** Drawing that is correct by test, with no hardware anywhere near it.

**Files.** `components/stark_gfx/{include/stark_gfx.h,gfx_surface.c,gfx_draw.c}`,
`test/host/{test_gfx.c,support/ppm_dump.c}`.

**Deps.** STARK-0005.

**Notes.**
* Types and API per ARCHITECTURE §6.4. RGB565, native endianness; the panel-side byte order is STARK-0015's problem.
* `origin_x/origin_y` translation is mandatory from the first commit — retrofitting it later would touch every call site.
* Clipping is checked in one shared helper; every primitive goes through it.
* No allocation: surfaces wrap caller-provided memory.

**Host tests.** The *gfx* list in [TESTING.md §2](TESTING.md); on failure, dump the
surface to `.ppm` as a test artifact.

**AC.**
1. Clipping is correct at all four edges and for fully-outside rectangles (no writes at all — verify with guard bytes around the buffer).
2. `origin` translation verified: the same logical draw lands in the correct rows for band 0 and band 3.
3. Coverage ≥ 85 %; zero ASan findings.

---

## STARK-0014 — `stark_gfx` text: 1bpp fonts

**Goal.** Readable text with measurable width.

**Files.** `components/stark_gfx/{gfx_font.c,include/gfx_font.h,fonts/font_mono16.c}`,
`tools/fontconv.py`, `test/host/test_text.c`.

**Deps.** STARK-0013.

**Notes.**
* One font in this task: 8×16 monospace, ASCII 0x20–0x7E, generated by `tools/fontconv.py` from a public-domain source font. **Record the source font and its licence** in `components/stark_gfx/fonts/README.md`; do not embed a font of unknown provenance.
* `gfx_text()` decodes UTF-8 and renders a fallback glyph for unmapped code points. Turkish/Latin-1 glyph coverage is V0.1 — the decoder ships now so the API never changes.
* `gfx_text_width()` must exactly match what `gfx_text()` draws (test it by rendering and measuring the painted extent).

**Host tests.** The *text* list in [TESTING.md §2](TESTING.md).

**Wokwi/manual.** none yet.

**AC.**
1. Width function and rendered extent agree for 20 varied strings including multi-byte UTF-8.
2. Rendering is clipped correctly mid-glyph at a surface edge.
3. Font provenance and licence documented.
4. `tools/fontconv.py` is reproducible: re-running it regenerates a byte-identical `.c`.

---

## STARK-0015 — ILI9341 bring-up

**Goal.** Pixels on the simulated panel.

**Files.** `components/stark_display/{include/stark_display.h,display_panel.c,Kconfig}`,
`diagram.json` (add `wokwi-ili9341`), `idf_component.yml` (add `espressif/esp_lcd_ili9341`).

**Deps.** STARK-0006, STARK-0013.

**Notes.**
* **Pin the component exactly:** `espressif/esp_lcd_ili9341: "==2.1.0"` in `idf_component.yml` — an exact version, not `^2.0.0`. Commit the resulting `dependencies.lock`. If 2.1.0 fails to build against ESP-IDF v6.1 or misbehaves during bring-up, fall back to **`"==2.0.2"`** (the documented fallback, ADR-0004), record the reason in `docs/DEVELOPMENT.md`, and re-commit the lock.
* `esp_lcd_panel_io_spi` on SPI2 + `esp_lcd_new_panel_ili9341`; rotation 1 (landscape 320×240) by default; expose `swap_xy`/`mirror_x`/`mirror_y`/`bgr`/`invert` via Kconfig with defaults that match Wokwi.
* Drive RST (GPIO 14) and backlight (GPIO 21, LEDC channel 1) even though Wokwi models neither (ADR-0010) — do not connect them in the diagram.
* This task's deliverable is a test pattern only: `stark_display_test_pattern()` filling colour bars + a 1 px border, called from `app_main` temporarily. The band pipeline is STARK-0016.
* Commit `dependencies.lock` with the resolved component version.

**Host tests.** none (pure port).

**Wokwi/manual.** Colour bars appear within 1 s of boot, correct orientation, correct
colours (red bar is red — if not, the BGR default is wrong; fix the default, do not
swap colours in `stark_gfx`).

**AC.**
1. 320×240 landscape test pattern renders correctly in Wokwi.
2. Panel init errors return `stark_err_t` and cause a logged `stark_panic()`, not a silent black screen.
3. `idf_component.yml` pins an **exact** version (`==2.1.0`, or `==2.0.2` with the reason recorded); `dependencies.lock` is committed and CI verifies the build does not modify it.
4. SPI clock is Kconfig-driven with a 40 MHz default and a documented 20 MHz breadboard recommendation.

---

## STARK-0016 — Band rendering pipeline

**Goal.** `stark_display_render(area, fn, ctx)` — the API the whole UI is built on.

**Files.** `components/stark_display/display_render.c`, `stark_display.h`.

**Deps.** STARK-0015, STARK-0014.

**Notes.**
* Two DMA-capable band buffers of `320 × STARK_DISPLAY_BAND_H × 2` bytes from `heap_caps_malloc(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)` at init; ping-pong so the CPU renders band N+1 while band N transfers.
* Walk only the bands intersecting `area`; set `surface.origin_*` per band; call `fn` once per band.
* Replace the temporary test-pattern call with a render callback that draws the same pattern through the new API, then delete the old path.
* Log `display: init 320x240 band=40 bufs=2x25600` once at init.

**Host tests.** The band-walking arithmetic (which bands intersect which area, and the
origin computed for each) extracted as a pure function and tested: single-band areas,
areas spanning exactly a boundary, full-screen, empty, out-of-bounds.

**Wokwi/manual.** The test pattern is pixel-identical to STARK-0015's, and a partial
render of a 50×30 region updates only that region.

**AC.**
1. Full-screen render produces no visible seams at band boundaries.
2. A partial render touches only the requested region (verified visually by rendering a coloured rect over an existing pattern).
3. Full-screen refresh duration is **logged** (`display: full refresh <N> ms`) and recorded in `docs/measurements.md` as informational. It is **not** a gate here — the ≤ 60 ms target is verified on hardware at V1 prototype (ADR-0011).
4. Band-walk host tests pass.
5. Rendering the same content via one full-screen call and via several partial calls produces an identical final image.

---

## STARK-0017 — `stark_ui`: screen stack & status bar

**Goal.** A navigable UI shell with damage tracking.

**Files.** `components/stark_ui/{include/stark_ui.h,ui_stack.c,ui_statusbar.c,
ui_theme.h}`.

**Deps.** STARK-0016, STARK-0010.

**Notes.**
* `stark_screen_t` and the stack API exactly as ARCHITECTURE §6.7. Stack depth 8, static.
* `stark_ui_tick()`: drain events → offer to the top screen → if damage is non-empty, call `stark_display_render()` with the union of damage and clear it.
* Status bar: 16 px tall, title from the top screen's `name`, right side reserved (battery/SD/clock arrive later — draw nothing there now, do not draw placeholders that will be mistaken for features).
* Colours centralised in `ui_theme.h` as named constants from the first commit.
* Frame cap: `STARK_UI_TARGET_FPS` (30) — sleep on `stark_event_wait()` with the remaining frame budget.

**Host tests.** none directly (the stack is thin; the model tests come with the menu).

**Wokwi/manual.** A trivial screen that fills its area and shows its title renders; a
second pushed screen replaces it; `stark_ui_pop()` restores the first with a correct
full redraw.

**AC.**
1. Push/pop works to depth 3 with correct redraws.
2. An unhandled BACK at the root is a no-op (no crash, no pop past zero).
3. No allocation in `stark_ui_tick()`.
4. Idle CPU: with no events, the UI task blocks and does not busy-render (verifiable by the absence of repeated render logs at DEBUG).

---

## STARK-0018 — List menu widget + menu model

**Goal.** The primary navigation surface.

**Files.** `components/stark_ui/{ui_menu.c,include/ui_menu.h,ui_menu_model.c,
include/ui_menu_model.h}`, `test/host/test_menu_model.c`.

**Deps.** STARK-0017, STARK-0011.

**Notes.**
* Split strictly: `ui_menu_model.c` is pure arithmetic (selection, scroll window, paging, wrap) and is host-tested; `ui_menu.c` renders it and handles keys.
* Rendering: item height 24 px, ~9 visible rows below the status bar, selection highlighted by inverted colours, a scroll indicator at the right when the list overflows.
* Damage: moving the selection invalidates **only** the two affected rows, not the screen. This is the task where that discipline is established.
* Log `menu: sel=<n> "<title>"` at INFO on every selection change (CI asserts on it).
* LEFT/RIGHT page by one screen; UP/DOWN wrap.

**Host tests.** The *menu model* list in [TESTING.md §2](TESTING.md).

**Wokwi/manual.** Navigate a 12-item list: wrap works, scrolling works, only the
affected rows repaint (visually verifiable by a deliberate slow render during
development).

**AC.**
1. Model host tests pass, coverage ≥ 90 %.
2. Selection move repaints exactly two rows.
3. `menu:` log line is emitted with the exact documented format.
4. A 1-item and a 0-item list render sensibly.

---

## STARK-0019 — `stark_app`: explicit static registry, lifecycle, launcher

**Goal.** Apps exist as independent modules and can be launched from the menu.

**Files.** `components/stark_app/{include/stark_app.h,include/app_list.h,
app_registry.c,app_manager.c}`.

**Deps.** STARK-0018.

**Notes.**
* **Explicit static registration (ADR-0009).** `app_list.h` declares `extern const stark_app_t app_<name>;` for each app; `app_registry.c` holds one array:
  ```c
  static const stark_app_t *const stark_apps[] = { &app_about, &app_inputtest, … };
  ```
  **No linker sections, no linker fragment, no `__start_/__stop_` symbol walking.** That approach was considered and rejected for V0 — it is harder to debug and can silently drop apps under section garbage collection. It stays a documented future option; do not implement it now.
* `stark_app_t` has **no `caps_required` field** in V0. Capability bits, module discovery and disabled menu entries are introduced as one coherent unit at V0.5 (STARK-0502/0505). Do not add the field, an enum, or a placeholder.
* `stark_app_launch(id)`: `on_start` → `stark_ui_push(screen)`; BACK from the app screen → `stark_ui_pop()` → `on_stop`. An `on_start` failure logs and returns to the menu — it must never panic.
* Log `app: start <id>` / `app: stop <id>` at INFO; log the whole registry at boot (`app: registry n=4 about,inputtest,…`).
* Root menu is built from the registry, sorted by category then title.

**Host tests.** Registry sorting/grouping as a pure function (stable order for equal
categories; empty registry; single entry), and `stark_app_find()` lookup including the
not-found case.

**Wokwi/manual.** Covered by STARK-0020.

**AC.**
1. Adding an app requires exactly one new directory plus one `extern` declaration and one array line — and nothing else.
2. Boot logs the full registry contents.
3. A deliberately failing `on_start` returns to the menu with a logged error and no crash (verify with a temporary fault-injection app, then remove it).
4. `grep -r "caps_required\|STARK_CAP_\|ld.fragment" components/ apps/` returns nothing.

---

## STARK-0020 — V0 applications: About, Input Test, Display Test, Buzzer Test

**Goal.** Four small apps that exercise the platform and prove the extension recipe.

**Files.** `apps/app_about/**`, `apps/app_inputtest/**`, `apps/app_displaytest/**`,
`apps/app_buzzertest/**`, `components/stark_buzzer/**`.

**Deps.** STARK-0019, STARK-0007.

**Notes.**
* `stark_buzzer` first: LEDC channel 0, `stark_buzzer_tone(hz, ms)` and a small
  non-blocking sequence player driven by `esp_timer`. UI feedback hooks: a 20 ms click
  on selection change, a 60 ms low buzz on a rejected BACK/disabled item.
* **About:** firmware version, IDF version, chip model/revision, flash size, free heap,
  uptime, board name. Refreshes once per second; the perfect place to prove partial
  redraws.
* **Input Test:** a live view of the six keys showing state and the last action, plus
  press counters. This is the app that makes input regressions obvious.
* **Display Test:** colour bars, a gradient, a 1 px grid, a text sample, and a
  full-screen refresh timer showing measured FPS (displayed and logged as
  informational — the FPS *gate* is a hardware criterion, ADR-0011).
* **Buzzer Test:** a few fixed tones and a short melody; OK plays, BACK exits.
* Each app ≤ ~150 lines. If one grows past that, the widget it wants belongs in
  `stark_ui` — raise it as a separate task rather than inflating the app.

**Host tests.** none (presentation only). Any formatting helper (e.g. uptime → string)
that appears goes in `stark_ui` with a host test.

**Wokwi/manual.** Launch each app from the menu, interact, return with BACK.

**AC.**
1. All four apps launch, render, respond to keys, and exit cleanly.
2. Heap after launching and exiting each app 10 times is within 1 kB of the baseline (no leaks) — checked via the About screen's free-heap reading.
3. Buzzer is audible in Wokwi and UI click feedback works.
4. Display Test renders correctly and reports a plausible, non-zero FPS figure; the measured value is recorded in `docs/measurements.md` as informational and is not a pass/fail threshold in simulation.
5. About reports the correct board name, 16 MB flash and the expected internal heap for a PSRAM-disabled build on the simulated N16R8 SKU.

---

## STARK-0021 — V0 integration, CI scenarios, and milestone close-out

**Goal.** V0 is verified end to end, automatically, and documented.

**Files.** `test/scenarios/{v0-boot.yaml,v0-boot-and-menu.yaml,v0-input-matrix.yaml}`,
`scripts/test_wokwi.sh`, `.github/workflows/ci.yml` (add the Wokwi job),
`docs/measurements.md`, `README.md` (status update).

**Deps.** STARK-0020.

**Notes.**
* Scenarios per [WOKWI.md §6](WOKWI.md) and [TESTING.md §4](TESTING.md). Keep each ≤ 20 s.
* `boot: ui_ready in <N> ms` is added to `stark_main` here if not already present. Scenarios assert on the **presence and ordering** of the line; they must not assert on `<N>` (ADR-0011, [TESTING.md §1.1](TESTING.md)).
* Add a boot-time `diag: heap=<n>` line for the heap assertion (this is the minimal diagnostics needed now; the full `stark_diag` component is V0.1 — do not build it here). Heap **is** a legitimate simulation gate.
* CI: full scenario set on pull requests, `v0-boot.yaml` only on push, to respect the simulation-minute quota.
* `docs/measurements.md` gets two clearly separated sections: **Simulated (informational)** — boot-to-menu, refresh time, FPS, binary size, heap; and **Hardware (gated, V1 prototype)** — left empty with the target values listed, to be filled in at that milestone.

**Host tests.** Whole suite must be green.

**Wokwi/manual.** All three scenarios pass headlessly via `scripts/test_wokwi.sh`.

**AC.**
1. Every V0 exit criterion in [ROADMAP.md](ROADMAP.md) is demonstrably met, with the evidence recorded in `docs/measurements.md`.
2. All three scenarios pass in CI, and none of them asserts on a wall-clock value.
3. `grep -ri wokwi components/ apps/ main/` returns nothing (no simulator conditionals).
4. Coverage report shows ≥ 80 % over the four pure cores.
5. `docs/measurements.md` separates simulated-informational from hardware-gated figures, with the V1 targets listed but unfilled.
6. README status updated from "planning" to "V0 complete".

---

# V0.1 — architecture hardening

Tasks are specified when V0 closes; the intended split (each one session):

| ID | Goal |
| --- | --- |
| STARK-0101 | Per-widget damage coalescing + frame-overrun counters |
| STARK-0102 | Modal dialog helper (confirm/alert) + `stark_ui_dialog` |
| STARK-0103 | Long-BACK → root, key-repeat tuning, navigation polish |
| STARK-0104 | Menu categories, disabled items, icons, paging |
| STARK-0105 | UTF-8 Latin-1/Turkish glyph coverage + second font size + text wrapping |
| STARK-0106 | App worker-task helper + `on_stop` join contract + crash containment |
| STARK-0107 | `stark_diag` v1 (heap/task/FPS/event drops) + Diagnostics app |
| STARK-0108 | V0.1 scenarios + measurements refresh |

# V0.2 — storage & settings

| ID | Goal |
| --- | --- |
| STARK-0201 | `stark_storage`: SD over shared SPI2, mount/unmount, bus arbitration |
| STARK-0202 | Hot-insert/removal events, safe-eject, graceful degradation |
| STARK-0203 | Path conventions + file helpers + `sim/diagram.storage.json` |
| STARK-0204 | `stark_settings` over NVS: typed keys, defaults, change events |
| STARK-0205 | Settings app (brightness, volume, repeat, log level, restore defaults) |
| STARK-0206 | Rotating file log sink for `stark_log` |
| STARK-0207 | V0.2 scenarios (persistence, 100 kB write under render load) |

# V0.3 — infrared lab

| ID | Goal |
| --- | --- |
| STARK-0301 | IR codec core (NEC/NEC-ext) + host tests with recorded vectors |
| STARK-0302 | IR codec core (RC5/RC6/SIRC) + raw timing format |
| STARK-0303 | `stark_ir` RMT RX driver (ESP-IDF v6 RMT API) |
| STARK-0304 | `stark_ir` RMT TX driver with carrier + safety gating |
| STARK-0305 | IR app: live receive view + save to SD |
| STARK-0306 | IR app: browse + replay saved signals (own devices only) |
| STARK-0307 | Hardware bring-up checklist + measurements |

# V0.4 — NFC lab

| ID | Goal |
| --- | --- |
| STARK-0401 | NDEF parse/serialise core + host tests |
| STARK-0402 | PN532-class driver over I²C: init, detect, UID/ATQA/SAK |
| STARK-0403 | Tag info app + technology identification |
| STARK-0404 | NDEF read/write to our own writable tags |
| STARK-0405 | Dump-to-SD format + hardware checklist |

# V0.5 — expansion / module bus

| ID | Goal |
| --- | --- |
| STARK-0501 | Module descriptor format + I²C ID EEPROM layout spec (first appearance of module discovery anywhere in the project) |
| STARK-0502 | `stark_module`: enumeration, **introduction of capability bits and `stark_app_t.caps_required`**, hotplug |
| STARK-0503 | SPI CS / IRQ arbitration for multiple modules |
| STARK-0504 | Retrofit IR and NFC behind the module interface |
| STARK-0505 | Module inspector app + capability-gated (disabled) menu entries |
| STARK-0506 | Optional: evaluate link-time app registration against the static array (ADR-0009 revisit) — decide and record, implement only if the app count justifies it |

# V0.6 — Sub-GHz laboratory module

| ID | Goal |
| --- | --- |
| STARK-0601 | CC1101 driver: SPI init, register profiles, GDO handling |
| STARK-0602 | RSSI meter + band scan view |
| STARK-0603 | OOK/2-FSK bitstream capture + raw-to-SD format |
| STARK-0604 | Generic non-security OOK decoders (host-tested codecs) |
| STARK-0605 | Gated replay of our own captures: confirmation, duty-cycle limiter, power cap, TX logging |
| STARK-0606 | Hardware checklist + RF measurements |

# V0.7 — diagnostics & lab tools

| ID | Goal |
| --- | --- |
| STARK-0701 | Safe-pin allowlist + GPIO app (read/write/toggle/pulse/PWM) |
| STARK-0702 | Frequency/duty measurement |
| STARK-0703 | I²C scanner app + register read/write + `sim/diagram.i2c.json` |
| STARK-0704 | UART tools: bridge, monitor, auto-baud, SD logging |
| STARK-0705 | SPI probe app |
| STARK-0706 | Self-test app (RAM/flash/SD/display/buttons/buzzer/modules) |
| STARK-0707 | V0.7 scenarios (I²C scan, UART loopback) |
| STARK-0708 | **LVGL re-evaluation** per ADR-0005 revisit trigger — decide and record |

---

## Task hygiene rules for the implementing agent

1. **One task per branch, one branch per PR.** The PR description names the task ID and
   copies its acceptance criteria as a checklist.
2. **Do not invent scope.** If a task seems to need something a later task owns, stop
   and say so; the plan gets amended rather than exceeded.
3. **Do not leave TODOs for planned work** — planned work lives in this file, not in
   comments. `TODO` in code is grounds for rejection; `NOTE:` explaining a non-obvious
   decision is welcome.
4. **Update the docs in the same change.** If the pin map moves, HARDWARE.md and
   `diagram.json` move with it, in that commit.
5. **If a decision is required that this plan does not answer,** write it up as a new
   ADR in DECISIONS.md and flag it in the PR instead of deciding silently.
6. **Never commit secrets, tokens, or capture files containing real identifiers.**
