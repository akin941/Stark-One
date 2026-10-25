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

**Scope source:** [ROADMAP.md](ROADMAP.md) "V0.1". Nothing outside that scope is planned
here; nothing from V0.2+ is pulled forward (no storage, no settings persistence, no
capability bits, no radios).

**Numbering.** V0.1 keeps the milestone-prefixed convention (`STARK-01NN`). The
provisional split listed here while V0 was open has been re-cut in execution order:
text work moves ahead of the dialog (the dialog wraps its message), theme and the
registry move ahead of the first new V0.1 app, and one infrastructure task
(`STARK-0100`) settles V0 debt first. Old → new: 0101 → 0101, 0105 → 0102, 0102 →
0105, 0103 → 0106, 0104 → 0107, 0106 → 0108, 0107 → 0109, 0108 → 0110; 0103 (theme)
and 0104 (registry + hello app) are new splits of ROADMAP scope items.

**Validation (ADR-0017).** Wokwi is optional and never required. Throughout V0.1 a
"Wokwi scenario" means an emulator scenario `test/emu/<same name>.toml` asserting the
same log lines (the soak is `test/emu/soak/v01-soak.toml`); a "screenshot" means a
host render check at the display boundary — the screen rendered into a memory surface
by the production drawing code on a host display test port and compared pixel by
pixel (docs/VALIDATION.md §3.3, §3.4); a "throwaway build" runs in the emulator. Evidence
that needs a real panel, switch, piezo or LED is labelled *physical, deferred to HIL*.
Per-task wording is updated as each task is taken up.

**Hardware.** None. V0.1 runs on the unchanged V0 circuit (`diagram.json`, HARDWARE.md
§2). No task needs a physical device; every behavioural criterion is verified in Wokwi,
every pure function on the host. Performance figures stay informational (ADR-0011).

**Exit-criteria map** (ROADMAP V0.1):

| Exit criterion | Proven by |
| --- | --- |
| 1. A new "hello" app in one new component directory, < 120 lines, no edits to `components/` | STARK-0104 (ADR-0016), re-checked in STARK-0110 |
| 2. Modal confirm dialog works and is covered by a Wokwi scenario | STARK-0105 (`v01-dialog.yaml`) |
| 3. Event-drop counters zero over a 60 s scenario; overruns reported, not gated | STARK-0109 (counters, `drops=` gate), STARK-0110 (`v01-soak.yaml`) |
| 4. Diagnostics app displays live heap and FPS | STARK-0109 |

**Planning debt audit** (recorded at V0 close; disposition decided here):

| Item | Disposition |
| --- | --- |
| `scripts/check_layers.py` referenced (ROADMAP V0 scope, ARCHITECTURE §10, TESTING §3, AGENTS.md) but never created | **V0.1 infrastructure**, STARK-0100 — V0.1 adds components and must not do so unguarded |
| Five `menu "STARK"` blocks produce five STARK menus instead of ARCHITECTURE §9's single one | **V0.1 infrastructure**, STARK-0100 |
| `-Wshadow -Wconversion -Wundef -Wdouble-promotion` promised for `stark_*` firmware components (TESTING §3) but only the host build uses any of them | **V0.1 infrastructure**, STARK-0100 (found during this audit) |
| `clang-tidy` gate and the ±10 % binary-size flag (TESTING §3/§7) never implemented | **Infrastructure debt register** below — not a V0.1 exit criterion; TESTING.md marks them *not yet active* |
| Physical performance measurements | **Deferred** to V1 prototype, unchanged (ADR-0011) |
| Push CI runs only `v0-boot` | **By design** (TESTING §4, quota) — not debt. V0.1 scenarios join the pull-request set; the 60 s soak runs on dispatch (STARK-0110) |
| No physical hardware validated | **Deferred** to V1 prototype; V0.1 needs none |

---

## STARK-0100 — V0 infrastructure debt: layer checker, one STARK menu, strict warnings

**Goal.** The three V0 guarantees that exist only on paper become mechanical before V0.1
adds components.

**Layer.** Tooling / build. No firmware behaviour changes.

**Files.** `scripts/check_layers.py` (new), `scripts/check.sh`, `.github/workflows/ci.yml`
(lint step), `CMakeLists.txt` (root: warning flags), `main/Kconfig.projbuild` (new),
`components/*/Kconfig` → `components/*/Kconfig.stark` (rename, content unchanged),
any `stark_*`/`app_*`/`main` source the new warnings flag, ARCHITECTURE.md §9/§10,
TESTING.md §3/§7, docs/DEVELOPMENT.md §7.

**Deps.** STARK-0021.

**Notes.**
* **Layer table** (component granularity, the one `check_layers.py` encodes and
  ARCHITECTURE §10 documents): `stark_err` universal; L0 `stark_board`; L1 `stark_hal`,
  `stark_log`; L2 `stark_gfx` (the only all-pure component); L3 `stark_event`,
  `stark_input` (each an L2 core plus an L3 port), `stark_display`, `stark_buzzer`;
  L4 `stark_ui`, `stark_app`; L5 every `apps/app_*`; `main` is the composition root and
  may require anything.
* **Rules:** a `REQUIRES`/`PRIV_REQUIRES` entry must name a strictly lower layer,
  `stark_err`, a platform (non-`stark_`, non-`app_`) component, or an edge on the
  explicit sideways allowlist — today exactly `stark_input → stark_event` and
  `stark_app → stark_ui`. `stark_gfx` and `stark_err` may require no platform
  component. No component other than `main` may require an `app_*`; apps may not
  require each other. A `stark_*` component missing from the table fails the check
  ("unclassified"), so every new component forces an explicit layer decision recorded
  in ARCHITECTURE §10 in the same change.
* Parse `idf_component_register(...)` argument lists from every
  `components/*/CMakeLists.txt`, `apps/*/CMakeLists.txt` and `main/CMakeLists.txt`
  (keywords `REQUIRES`, `PRIV_REQUIRES`; tokens end at the next upper-case keyword).
  Standard library only; output names the offending file and edge.
* **One STARK menu.** ESP-IDF renders each component `Kconfig` as its own menu, so five
  `menu "STARK"` blocks give five menus. Rename each component's file to
  `Kconfig.stark` (ESP-IDF no longer auto-includes it) and add one
  `main/Kconfig.projbuild` holding `menu "STARK"` with one explicit `rsource` line per
  component file — explicit like the app registry, no globbing. Symbol names,
  defaults, ranges and help text do not change.
* **Warnings.** Enable `-Wshadow -Wconversion -Wundef -Wdouble-promotion` on every
  `stark_*` component, every `app_*` and `main`, from one place: after `project()` in
  the root `CMakeLists.txt`, walk `BUILD_COMPONENTS` and add the flags `PRIVATE` to
  each matching non-interface `COMPONENT_LIB`. Fix what they find in our code. A flag
  whose only diagnostics originate *inside vendor headers* and cannot be fixed in our
  code is left disabled, with the evidence (flag, header, sample diagnostic) written
  into TESTING.md §3 — no `-Wno-*`, no pragma, no owner decision needed.

**Host tests.** Whole suite green (unchanged).

**Emulator/runtime.** The three emulator scenarios (`test/emu/v0-*.toml`) pass unchanged
(behaviour must not move). Evidence also accepted: the runtime artifact rebuilt
byte-identical to the pre-task `main` under the same pinned toolchain and inputs, which
carries the earlier runtime evidence over (ADR-0017).

**AC.**
1. `python3 scripts/check_layers.py` passes on the tree and runs in `scripts/check.sh`
   and the CI `lint` job.
2. On throwaway copies, each of these fails naming the edge: `stark_ui` added to
   `stark_display`'s `REQUIRES`; `app_about` added to `stark_app`'s; `app_about` added to
   `app_inputtest`'s; a new `components/stark_x/` not in the table; `esp_timer` added to
   `stark_gfx`.
3. After `rm -rf build sdkconfig`, `grep -c '^# STARK$' sdkconfig` is `1`, and the
   `CONFIG_STARK_*` lines of `build/config/sdkconfig.h` are identical to STARK-0021's.
4. The flags enabled under the Notes rule reach every `stark_*`, `app_*` and `main` compile
   command in `build/compile_commands.json` and no vendor component's; the build has 0
   warnings. (Outcome: `-Wshadow -Wconversion -Wdouble-promotion` enabled; `-Wundef` left
   off with its vendor-header evidence in TESTING §3.)
5. `grep -rn "Wno-\|diagnostic ignored" components/ apps/ main/` returns nothing new.
6. ARCHITECTURE §9/§10, TESTING §3/§7 and docs/DEVELOPMENT.md describe the checker, the
   sideways allowlist and the flags as they now are.

**Out of scope.** clang-tidy, size-regression reporting (debt register), any runtime
change, new Kconfig symbols.

**Integration.** Every later V0.1 task that adds a component or a sideways edge updates
the table and ARCHITECTURE §10 in the same commit (STARK-0109 adds `stark_diag`).

**CI.** `lint` gains `check_layers.py`; firmware compile flags change (a failing warning
now fails `firmware`).

**Hardware.** None. **Security.** None.

---

## STARK-0101 — Multi-rect damage coalescing + UI frame statistics

**Goal.** Distant invalidations stop repainting everything between them, and every
frame is counted so overruns and render failures are observable.

**Layer.** L4 (`stark_ui`) with a pure, host-tested damage set (like `ui_menu_model`).

**Files.** `components/stark_ui/{ui_damage.c,include/ui_damage.h,ui_stack.c,include/stark_ui.h,CMakeLists.txt}`,
`test/host/{test_ui_damage.c,CMakeLists.txt}`, ARCHITECTURE.md §6.7.

**Deps.** STARK-0100.

**Notes.**
* `ui_damage_t` holds up to `UI_DAMAGE_MAX` (4) non-overlapping rectangles.
  `ui_damage_add()` ignores empty rects; merges the new rect with every stored rect it
  overlaps or shares an edge with (cascading until no stored pair overlaps or touches);
  appends otherwise; and, when that would exceed 4, merges the pair (over the stored
  rects plus the new one) whose union grows total area least — ties to the lowest index
  pair. `ui_damage_clear()`, `ui_damage_empty()`, iteration by index. Pure, no
  ESP-IDF, 32-bit arithmetic like `gfx_clip()`.
* `stark_screen_t.damage` becomes a `ui_damage_t`; screens keep using
  `stark_ui_invalidate()` (zero-initialised = empty, so existing designated
  initialisers stay valid). Each tick builds the frame set (top screen's damage plus
  status-bar damage), clears the sources, and calls `stark_display_render()` once per
  rect, clipped to the display. Rects never overlap, so no pixel is drawn twice.
* **Frame statistics:** `stark_ui_stats_t { uint32_t frames, renders, overruns,
  render_errors, last_frame_us, max_frame_us; }` and `stark_err_t
  stark_ui_stats(stark_ui_stats_t *out)`. A frame is a tick that rendered; `renders`
  counts display calls; an overrun is a rendering tick whose dispatch-plus-render time
  exceeds `1 000 000 / CONFIG_STARK_UI_TARGET_FPS` µs; a failed
  `stark_display_render()` increments `render_errors` (and keeps the existing
  `ui: render failed:` log). The UI task is the counters' only writer; each is a C11
  atomic, so any task may snapshot them (STARK-0109 reads them from the `esp_timer`
  task) — an individual counter is always consistent, a snapshot may straddle one
  frame. (Atomics rather than a `portMUX` keep `stark_ui` buildable on the host UI test
  port.) Each overrun logs `ui: overrun <us> us` at DEBUG.
* Replace the single `ui: render x,y wxh` DEBUG line with one per rect.

**Host tests.** `test_ui_damage.c`: disjoint rects stay separate; overlap and shared
edge merge; cascade (a rect bridging two stored rects leaves one); fifth rect triggers
the least-growth merge (and its tie rule); empty/negative rects ignored; a seeded
property test (≥ 1 000 random sequences on a 64×48 canvas) checks after every add that
stored rects never overlap and cover every added pixel. 100 % lines of `ui_damage.c`.

**Host UI test port / emulator** (ADR-0017; docs/VALIDATION.md §3.3).
* `test_ui_launcher.c`: in the launcher (four apps, list fits), UP from the first item
  wraps to the last and renders exactly two 24 px row rects, not one spanning rect; the
  launcher golden is unchanged.
* Frame statistics on the port's fake clock: an app screen whose render advances the
  clock past the 33 333 µs budget counts one overrun (and `max_frame_us` records it); a
  frame inside the budget counts none; an idle tick counts no frame; a render forced to
  fail increments `render_errors` by one; `frames`/`renders` match the render calls
  seen. Deterministic, host-only — overrun *values* stay informational (ADR-0011).
* The three emulator scenarios pass unchanged.

**AC.**
1. Host tests pass; `ui_damage.c` at 100 % line coverage.
2. The wrap move repaints two rows (two rects), shown by the host UI port's render log.
3. `overruns` and `render_errors` count as specified (host UI port assertions).
4. `stark_ui_stats()` is callable from any task (atomic counters; documented in the
   header); STARK_ERR_INVALID_ARG for NULL.
5. No allocation in the render path; V0 scenarios green; ARCHITECTURE §6.7 updated.

**Out of scope.** Displaying the stats (STARK-0109), a widget tree, damage inside
`stark_display`, changing band height or FPS.

**Integration.** STARK-0105 (overlay dialog) and STARK-0109 (diag) consume this.

**CI.** Adds a host test file. **Hardware.** None; overrun *gating* is V1. **Security.** None.

---

## STARK-0102 — Text: Latin-1 + Turkish glyphs, a 6×10 font, wrapping

**Goal.** Every Latin-1 and Turkish letter renders, a smaller font exists for dense
screens, and text can wrap into a box.

**Layer.** L2 (`stark_gfx`, pure) + host tooling.

**Files.** `components/stark_gfx/fonts/{misc-fixed-8x13-latin.bdf,font_mono16.c,misc-fixed-6x10-latin.bdf,font_mono10.c,README.md}`
(the `-ascii` subset is replaced), `components/stark_gfx/{gfx_font.c,include/gfx_font.h,CMakeLists.txt}`,
`tools/fontconv.py`, `scripts/check.sh` (font-reproducible for both fonts),
`test/host/{test_text.c,test_wrap.c,CMakeLists.txt}`, `apps/app_displaytest/app_displaytest.c`
(text samples), ARCHITECTURE.md §6.4.

**Deps.** STARK-0100.

**Notes.**
* **Coverage:** both fonts span U+0020..U+017F (Basic Latin, Latin-1 Supplement, Latin
  Extended-A) — includes Ç ç Ğ ğ İ ı Ö ö Ş ş Ü ü. `gfx_font_t` stays a contiguous
  range: `fontconv.py generate --fill-missing` writes the fallback glyph's bitmap for
  every code point in range the source lacks (U+007F..U+009F), so "unmapped → fallback"
  still holds and the struct and `gfx_text()` do not change.
* **Sources:** X.Org misc-fixed `8x13` (as STARK-0014) and `6x10`, both from Ubuntu
  24.04 `xfonts-base`, converted with `pcf2bdf`, subset to the range above. Verify the
  6x10 BDF's `COPYRIGHT` property says public domain before using it; if it does not,
  stop and write an ADR (public domain is the V0 font rule). README records package,
  version and sha256 of each source, like STARK-0014.
* **Second font:** `gfx_font_mono10` — 6×10 cells, baseline from the BDF's
  `FONT_ASCENT`, fallback `?`. Flash cost ≈ 352 × 10 B; mono16 grows to ≈ 352 × 16 B.
* **Wrapping** (pure, in `gfx_font.c`):
  `size_t gfx_text_line(const gfx_font_t *f, const char *utf8, int16_t max_w, const char **next)`
  returns the byte length of the first line that fits `max_w`, breaking after the last
  U+0020 that fits, at `\n` (consumed, never drawn), or mid-word when one word is wider
  than `max_w`; a line always holds at least one code point (no infinite loop at tiny
  widths); spaces at a break are skipped into `*next`; multi-byte sequences are never
  split and each malformed byte counts as one cell (as `gfx_text()` draws it).
  `int16_t gfx_text_lines(f, utf8, max_w)` counts lines;
  `int16_t gfx_text_box(s, f, gfx_rect_t box, int16_t line_gap, utf8, fg, bg, transparent)`
  draws top-aligned lines inside `box` (line pitch `f->h + line_gap`), stops before a
  line that would cross the box bottom, and returns the lines drawn.
* Display Test adds two sample lines: `ÇĞİÖŞÜ çğıöşü äéñß` in mono16 and the same in
  mono10.

**Host tests.** Every Turkish letter and a Latin-1 sample render their own glyph (bitmap
≠ fallback); U+0080 renders the fallback; mono10 widths are 6 px per code point;
wrap: exact fit, one-cell overflow, long word hard-broken, runs of spaces,
leading/trailing spaces, `\n`, multi-byte character at the break, `max_w` below one
cell, empty string, NULL font/string/next; `gfx_text_box` clips at the box bottom and its
return equals `gfx_text_lines` when everything fits. `gfx_font.c` ≥ 95 % lines.

**Host UI test port / emulator** (ADR-0017). `test_ui_displaytest.c` runs the real
Display Test screen on the host UI port: both sample rows (mono16 at y 180, mono10 at
y 200) are pixel-identical to a direct `gfx_text()` render of the same string, and the
rendered frame is inspected once. `test/emu/v01-displaytest.toml` launches Display
Test on the production image (new font tables on the target, continuous refresh, no
fault) and leaves it. Panel glass: physical, deferred to HIL.

**AC.**
1. Host tests pass at the coverage above.
2. `scripts/check.sh` regenerates both subsets and both C tables byte-identically.
3. On the host UI port, Display Test's two sample rows match the direct render.
4. Font provenance and licence for both fonts are documented; both are public domain.
5. Flash growth recorded in the PR (informational); heap unchanged (fonts are `const`).

**Out of scope.** Proportional or anti-aliased fonts, bold, glyphs above U+017F,
combining characters, RTL, hyphenation, text input.

**Integration.** STARK-0105 (dialog message) and STARK-0109 (Diagnostics, mono10) use it.

**CI.** Adds host tests; `lint`'s `check.sh` covers the second font. **Hardware.** None.
**Security.** None; licensing: public-domain sources only.

---

## STARK-0103 — Theme centralisation

**Goal.** Apps draw with the platform's palette and layout, never with literal colours,
the literal 320/240 or the status-bar height.

**Layer.** L4 (`stark_ui`) public header; L5 apps migrated.

**Files.** `components/stark_ui/include/stark_theme.h` (new, public; replaces the
private `ui_theme.h`), `components/stark_ui/{ui_stack.c,ui_menu.c,ui_statusbar.c}`,
`components/stark_ui/include/stark_ui.h` (content-area helper),
`apps/app_{about,inputtest,displaytest,buzzertest}/*.c`, ARCHITECTURE.md §6.7.

**Deps.** STARK-0101.

**Notes.**
* `stark_theme.h` holds every UI colour and layout constant: `STARK_THEME_BG`, `_FG`,
  `_ACCENT`, `_WARN`, `_DISABLED`, `_SEL_BG/_SEL_FG`, status-bar colours, scroll colours,
  `STARK_THEME_STATUSBAR_H`, row height, text insets. Existing values keep their current
  colours (no visual change). `ui_theme.h` is removed and its users include the public
  header.
* `gfx_rect_t stark_ui_content_rect(void)` — the area below the status bar, from
  `stark_display_width/height()`.
* Apps replace literal colours with theme names and literal geometry (`320`, `16`,
  `224`) with `stark_ui_content_rect()`. Display Test keeps its literal test-pattern
  colours (colour bars and gradient are the *subject* of that app) — they stay, named
  locally.

**Host tests.** None (constants); the suite stays green.

**Host UI test port** (ADR-0017). Frame goldens recorded **before** the change —
launcher, Input Test (idle and with a key held), Buzzer Test, Display Test — are
unchanged after it (`test_ui_launcher.c`, `test_ui_apps.c`). About needs ESP-IDF
chip/flash/heap APIs and cannot run on the host: `_Static_assert`s pin the theme values
it now uses to its former literals, and the emulator scenarios still launch it.

**AC.**
1. No app contains a literal colour, display dimension or status-bar height other than
   Display Test's named pattern colours (review grep for `GFX_RGB565`, `320`, `240`,
   `16 +` in `apps/` attached to the PR).
2. No UI colour or layout literal remains in `components/stark_ui/*.c`.
3. The before/after frames are identical for the launcher and the four apps (host
   goldens + About's pinned values).
4. V0 scenarios green; ARCHITECTURE §6.7 names `stark_theme.h` as the palette contract.

**Out of scope.** Runtime theme switching, dark/light themes, new colours beyond what
STARK-0105/0107 need (they add theirs to this header when they arrive — so `_ACCENT`
and `_WARN` from the notes land with their first user, not here).

**Integration.** Every later V0.1 widget and app uses `stark_theme.h`.

**CI.** None new. **Hardware.** Colours are verified physically at V1 (BGR flag).
**Security.** None.

---

## STARK-0104 — App registry at the composition root + hello app (ADR-0016)

**Goal.** Adding an app touches no file under `components/` (ROADMAP V0.1 exit 1), and a
minimal hello app proves it.

**Layer.** L4 (`stark_app` API), composition root (`main`), L5 (`apps/app_hello`).

**Files.** `components/stark_app/{app_registry.c (removed),include/app_list.h (removed),app_manager.c,app_internal.h,include/stark_app.h}`,
`main/{app_registry.c,app_registry.h,CMakeLists.txt,stark_main.c}` (new registry),
`apps/app_hello/**` (new), `apps/README.md`, `test/scenarios/*.yaml` (launcher order),
ARCHITECTURE.md §6.8/§11, DECISIONS.md (ADR-0016 status → implemented).

**Deps.** STARK-0103.

**Notes.**
* ADR-0016: the explicit static array and its `extern` declarations move from
  `components/stark_app/` to `main/app_registry.{c,h}`; `stark_app_init(const
  stark_app_t *const *apps, size_t count)` receives it (dependency injection from the
  composition root). Everything ADR-0009 decided stays: explicit, static, one line per
  app, no linker sections, registry logged at boot.
* `app_hello`: id `hello`, title `Hello`, category `Examples`; one screen that draws
  "Hello, STARK ONE" and the uptime with theme colours, refreshing once a second through
  the About pattern; < 120 lines including its `CMakeLists.txt`. It is the reference
  for the extension recipe and stays in the firmware.
* Launcher order changes (category "Examples" sorts first until STARK-0107); update the
  V0 scenarios' `menu: sel=` expectations in this commit (log contract, TESTING §4).

**Host tests.** `test_app_catalog.c` unchanged and green.

**Emulator / host** (ADR-0017). `test/emu/v01-hello.toml`: `app: registry n=5 …,hello`
at boot, OK launches Hello (the first launcher row), `hello: uptime=` logs its
once-a-second refresh, BACK → `app: stop hello`. The host UI port's tests inject their
registries through `stark_app_init()` like `main` does (NULL array/entry rejected).

**AC.**
1. `git diff --stat` of the commit that adds `apps/app_hello/` plus its registry line
   touches only `apps/app_hello/**` and `main/app_registry.{c,h}` — shown by making it a
   separate commit in the PR (every commit carries the `STARK-0104:` subject; a third
   commit carries the scenario-expectation and doc updates the new launcher order
   implies); `wc -l apps/app_hello/*` < 120.
2. `grep -rn "app_about\|app_hello" components/` returns nothing.
3. Hello launches, renders, exits; V0 scenarios updated and green.
4. `check_layers.py` still passes (main may reference apps; nothing else does).
5. ARCHITECTURE §6.8/§11 and `apps/README.md` describe the new two-step recipe;
   ADR-0016 records the change and ADR-0009 points to it.

**Out of scope.** Link-time registration (ADR-0009 revisit, STARK-0506), capability
fields (V0.5), more than one screen per app.

**Integration.** Every later app (App Test, Diagnostics) follows the new recipe.

**CI.** Scenario expectations change. **Hardware.** None. **Security.** None.

---

## STARK-0105 — Modal dialog: confirm and alert

**Goal.** A modal confirm/alert helper any screen can open, built to be the future
transmit-confirmation primitive (docs/SECURITY_SCOPE.md §4.2).

**Layer.** L4 (`stark_ui`); L5 test app.

**Files.** `components/stark_ui/{ui_dialog.c,ui_dialog_layout.c,include/ui_dialog.h,ui_stack.c,include/stark_ui.h,include/stark_theme.h,CMakeLists.txt}`,
`apps/app_apptest/**` (new), `main/app_registry.{c,h}`,
`test/host/{test_dialog_layout.c,CMakeLists.txt}`, `test/scenarios/v01-dialog.yaml`,
ARCHITECTURE.md §6.7, TESTING.md §4 (prefix).

**Deps.** STARK-0102, STARK-0104.

**Notes.**
* API (`ui_dialog.h`):
  `typedef enum { STARK_DIALOG_CANCEL, STARK_DIALOG_OK } stark_dialog_result_t;`
  `typedef void (*stark_dialog_done_fn)(stark_dialog_result_t r, void *ctx);`
  `stark_err_t stark_ui_dialog_confirm(const char *title, const char *message, stark_dialog_done_fn done, void *ctx);`
  `stark_err_t stark_ui_dialog_alert(const char *title, const char *message, stark_dialog_done_fn done, void *ctx);`
  One static dialog: STARK_ERR_BUSY while one is open, STARK_ERR_STATE before
  `stark_ui_init()`, STARK_ERR_INVALID_ARG for NULL title. Title (≤ 31 bytes) and
  message (≤ 159 bytes) are **copied**, truncated on a UTF-8 boundary.
* **Keys.** Confirm: OK short → OK; BACK short → CANCEL; every other key is consumed
  and ignored. Alert: OK or BACK short → OK. An OK/BACK short counts only if its PRESS
  arrived while the dialog was open, so a queued or in-flight key can never confirm.
  No timeout, no default-OK, no auto-confirm — confirmation is always a deliberate new
  OK press. `done` (optional) runs after the dialog is popped, so it may push a screen
  or open another dialog.
* **Overlay rendering.** `stark_screen_t` gains `bool overlay`. While an overlay is on
  top, each band renders the screen beneath (clipped to the content area) and then the
  overlay; events go only to the overlay; its push redraws the full content once, later
  frames repaint only the overlay's damage. An overlay above an overlay is rejected
  (STARK_ERR_BUSY).
* **Layout** (pure `ui_dialog_layout()`, host-tested): 280 px wide box centred in the
  content area; title row (mono16), message wrapped with `gfx_text_lines()` to at most
  4 lines (the rest clipped), a hint row `OK: confirm  BACK: cancel` (alert: `OK`) in
  mono10; 2 px accent border. Colours join `stark_theme.h`.
* Logs (INFO, reserved prefix `dialog:`): `dialog: open "<title>"`, `dialog: ok`,
  `dialog: cancel`.
* **App Test** (`apps/app_apptest`, id `apptest`, title `App Test`, category `Tests`): a
  `ui_menu` with `Confirm dialog` (OK: logs `apptest: confirmed` and, from `done`, opens
  an alert titled `Done`; cancel: logs `apptest: cancelled`) and `Alert dialog` (logs
  `apptest: alert closed`). ≤ 150 lines. STARK-0108 extends it.

**Host tests.** `test_dialog_layout.c`: box centred and inside the content area; 1-, 4-
and 6-line messages (6 clips to 4); empty message; title longer than the box truncates
at a code-point boundary. 100 % lines of `ui_dialog_layout.c`.

**Emulator / host UI port** (ADR-0017). `test/emu/v01-dialog.toml` on the production
image: App Test → `Confirm dialog` → `dialog: open "Confirm"` → BACK → `dialog: cancel`,
`apptest: cancelled`, no `app: stop` → OK again → OK → `dialog: ok`, `apptest: confirmed`,
`dialog: open "Done"` → OK → `dialog: ok` → `Alert dialog` → OK → `apptest: alert closed`
→ BACK → `app: stop apptest`. `test_ui_dialog.c` on the host UI port: border pixels at
the computed box, App Test's menu visible outside it, status bar unchanged, the copied
message rendered exactly, BUSY cases, and the held-key rule (a press delivered before
the dialog opens, its SHORT after — which only a test port can schedule exactly).

**AC.**
1. `v01-dialog.toml` passes in CI; on the host UI port the dialog's pixels match the
   layout.
2. Holding OK while opening the dialog (press before open, release after) does **not**
   confirm — on the host UI port, `done` is not called and no `dialog: ok` is logged
   until a fresh OK press.
3. Dialog strings are copied (the App Test caller passes a stack buffer; documented).
4. `done` may open a dialog: the `Done` alert opens from the confirm's `done` in the
   scenario (no BUSY).
5. Host tests pass at the coverage above; ARCHITECTURE §6.7 documents overlays;
   TESTING §4 lists `dialog:`.

**Out of scope.** Buttons with focus, text input, lists inside dialogs, dialog stacking,
toasts/timeouts, transmit logic itself (V0.3/V0.6 use this dialog).

**Integration.** STARK-0106 cancels dialogs on long-BACK; STARK-0108 shows the fault
alert; radio milestones build their TX confirmation on it.

**CI.** New scenario in the pull-request set; new host test. **Hardware.** None.
**Security.** This is the SECURITY_SCOPE §4.2 confirmation primitive: default is
cancel, confirmation needs a fresh deliberate OK, nothing confirms on its own.

---

## STARK-0106 — Navigation: long-BACK home, repeat timing, OK+BACK chord reservation

**Goal.** BACK held returns to the root from anywhere, repeat timing is tunable at
build time without code edits, and OK+BACK is reserved for a future soft reset.

**Layer.** L2 (`input_core`), L3 (`stark_input` service, Kconfig), L4 (`stark_ui`).

**Files.** `components/stark_input/{input_core.c,include/input_core.h,include/stark_input.h,input_service.c,Kconfig.stark}`,
`components/stark_ui/{ui_stack.c,include/stark_ui.h,ui_menu.c}`, `main/Kconfig.projbuild`
(if the menu lists files), `test/host/test_input_core.c`,
`test/emu/v01-navigation.toml`, ARCHITECTURE.md §6.6/§6.7.

**Deps.** STARK-0105.

**Notes.**
* **Timing injection:** `typedef struct { uint16_t debounce_ms, long_ms, repeat_delay_ms,
  repeat_interval_ms; } input_core_timing_t;` and `stark_err_t input_core_init(input_core_t *c,
  const input_core_timing_t *t)` (NULL = the current defaults 20/500/400/120;
  STARK_ERR_INVALID_ARG unless `repeat_interval_ms ≥ 40`, `repeat_delay_ms ≥ debounce_ms`,
  `long_ms > debounce_ms`). The core stays Kconfig-free. The service passes
  `CONFIG_STARK_INPUT_REPEAT_DELAY_MS` (400, range 150–1000) and
  `CONFIG_STARK_INPUT_REPEAT_INTERVAL_MS` (120, range 40–500). Runtime adjustment (a
  settings value) is V0.2's, which owns the settings consumer.
* **Chord:** when OK and BACK are both debounced-pressed (the second press arrives while
  the first is held), the core emits one `STARK_KEY_CHORD` action (new, appended to
  `stark_key_action_t`; `key = STARK_KEY_OK`) and suppresses SHORT and LONG for both keys
  until both are released (PRESS/RELEASE still flow). The input service logs
  `key: OK+BACK chord`. `stark_ui` consumes the chord globally and logs
  `ui: chord reserved` — no other effect in V0.1.
* **Long-BACK home:** `stark_ui` intercepts BACK `LONG` before the top screen sees it
  and calls `stark_err_t stark_ui_pop_to_root(void)` (public — STARK-0108 reuses it): pops
  every screen above the root top-down, each through the normal pop path (so app
  `on_stop` runs and an open dialog's `done` gets CANCEL), then redraws the root.
  Logs `ui: home depth=<n>`; at the root, a reject buzz and `ui: home at root`.
* **Polish:** LEFT/RIGHT paging that cannot move (already at an end) gives the reject
  buzz instead of silence.

**Host tests.** `test_input_core.c`: NULL timing = old behaviour (every existing case
green); custom timing honoured (delay/interval edges); invalid timing rejected; chord in
both orders; chord suppresses SHORT and LONG for both keys; release of one chord key
then re-press of it while the other is still held does not re-emit CHORD until both
were released; OK then BACK after OK's LONG still chords (BACK's LONG suppressed).
`input_core.c` stays at 100 % lines.

**Emulator / host UI port** (ADR-0017). `test/emu/v01-navigation.toml` on the production
image: App Test → `Confirm dialog` → BACK held 700 ms → `key: BACK long`,
`dialog: cancel`, `app: stop apptest`, `ui: home depth=2`; held again at the root →
`ui: home at root`; OK+BACK together → `key: OK+BACK chord`, `ui: chord reserved`, and
no `app: start` or `key: OK short` after it (`forbid_after`). `test_ui_navigation.c` on
the host UI port: the same unwinding and its log order, the reject buzz at the root,
the chord activating nothing, `stark_ui_pop_to_root()` directly, and the paging reject.

**AC.**
1. Host tests pass at 100 % of `input_core.c`.
2. `v01-navigation.toml` passes in CI.
3. Kconfig ranges enforce valid timing; defaults reproduce V0 timings exactly (the V0
   input-matrix scenario passes unchanged).
4. `stark_ui_pop_to_root()` documented (never pops the root; STARK_ERR_STATE before
   init); ARCHITECTURE §6.6/§6.7 updated (chord, long-BACK, timing).

**Out of scope.** The soft reset itself, runtime repeat settings (V0.2), repeat
acceleration, other chords.

**Integration.** STARK-0108 crash containment uses `stark_ui_pop_to_root()`.

**CI.** New scenario. **Hardware.** "Repeat timing feels right" is V1 checklist item 5;
the Kconfig makes that tuning a config change. **Security.** OK+BACK is reserved so no
app can bind it (future soft reset must not be preemptable by an app).

---

## STARK-0107 — Menu: category headers, icons, paging across headers

**Goal.** The launcher groups apps under category headers with icons, in an order the
registry controls.

**Layer.** L4 (`stark_ui` menu, `stark_app` launcher); L5 apps (icons, categories).

**Files.** `components/stark_ui/{ui_menu.c,ui_menu_model.c,include/ui_menu.h,include/ui_menu_model.h,include/stark_theme.h}`,
`components/stark_app/{app_catalog.c,app_internal.h,app_manager.c,include/stark_app.h}`,
`apps/*/app_*.c` (icon + category), `main/app_registry.c` (order),
`test/host/{test_menu_model.c,test_app_catalog.c}`, `test/scenarios/*.yaml`,
ARCHITECTURE.md §6.7/§6.8.

**Deps.** STARK-0106.

**Notes.**
* `ui_menu_item_t` gains `bool header` (a section title: accent colour, never selected
  or activated) and `const uint8_t *icon` (optional 16×16 1bpp, `gfx_blit_1bpp` layout,
  drawn at the row's left; labels shift right when the menu has any icon). `disabled`
  stays a generic rendering state — nothing sets it from hardware before V0.5.
* Model: header rows are non-selectable (reusing the enabled-callback path, so moves
  and paging already skip them). New rule: when the selection lands on the first row
  of the window and the row above it is non-selectable, the window scrolls up one so
  that header stays visible. Paging never lands on a header.
* `stark_app_t` gains `const uint8_t *icon` (optional). Every built-in app gets an icon.
* **Category order = registry order:** categories appear in the order their first app
  appears in the registry; apps within a category sort by title (stable). Pure
  `app_catalog_layout()` produces the header/app sequence (host-tested). Registry order
  becomes about, (diagnostics at STARK-0109), inputtest, displaytest, buzzertest,
  apptest, hello → **System**: About; **Tests**: App Test, Buzzer Test, Display Test,
  Input Test; **Examples**: Hello.
* Test apps move to category `Tests`. `menu: sel=<n>` indexes rows including headers;
  update every scenario's expectations in this commit.

**Host tests.** Model: header reveal on upward move and on wrap to the top; paging over
headers; all-header list has no selection. Catalog: category order by first
appearance; stable title sort within; empty categories impossible; NULL category
groups as "". `ui_menu_model.c` and `app_catalog.c` ≥ 95 % lines.

**Host UI test port / emulator** (ADR-0017). `test_ui_apps.c` pins the launcher with the
"Tests" header and the real apps' icons (frame golden, inspected), checks the header's
accent pixels and one icon bit for bit at its computed position; `test_ui_launcher.c`
pins the headered V0-style launcher. Every emulator scenario follows the new rows
(`v0-boot-and-menu` navigates across the Tests header).

**AC.**
1. Host tests pass at the coverage above.
2. The launcher frame on the host UI port matches its golden, with header and icon
   pixels asserted explicitly.
3. All scenarios updated and green.
4. `stark_app_t.icon` optional (NULL renders no icon, label unshifted only if no item
   has one); ARCHITECTURE §6.7/§6.8 updated.

**Out of scope.** Nested submenus, capability-driven disabling (V0.5), per-user
ordering, icon files on storage.

**Integration.** Diagnostics (STARK-0109) slots into System by registry position.

**CI.** Scenario expectations change. **Hardware.** None. **Security.** None.

---

## STARK-0108 — App lifecycle: worker helper, join contract, crash containment

**Goal.** Apps can run background work safely, stopping an app always joins its worker
before `on_stop`, and a failing app unwinds to the launcher instead of panicking.

**Layer.** L4 (`stark_app`); L5 App Test.

**Files.** `components/stark_app/{app_worker.c,app_fault.c,app_manager.c,app_internal.h,include/stark_app.h,Kconfig.stark,CMakeLists.txt}`,
`main/Kconfig.projbuild`, `apps/app_apptest/**`, `test/host/{test_app_fault.c,CMakeLists.txt}`,
`test/emu/v01-app-lifecycle.toml`, ARCHITECTURE.md §6.8/§8.

**Deps.** STARK-0107.

**Notes.**
* **Worker:** `typedef void (*stark_app_worker_fn)(void *ctx);`
  `stark_err_t stark_app_worker_start(stark_app_worker_fn fn, void *ctx);` (UI task only,
  while an app runs; one worker per app: STARK_ERR_BUSY; no app: STARK_ERR_STATE) and
  `bool stark_app_worker_should_stop(void);` (the worker polls it). One static slot —
  `StaticTask_t` + a `CONFIG_STARK_APP_WORKER_STACK` (4096) byte stack, priority
  `CONFIG_STARK_APP_WORKER_PRIO` (4, below the UI's 5), pinned to core 0 (the UI is on
  core 1). No allocation.
* **Join contract:** stopping an app (BACK, long-BACK, fault) sets the stop flag, waits
  up to `CONFIG_STARK_APP_WORKER_JOIN_MS` (1000) for the worker to return, then calls
  `on_stop` — so `on_stop` may free what the worker used. The wrapper signals completion
  and suspends itself; the joiner deletes the suspended task, so the static buffers are
  never reused while FreeRTOS still owns them. On timeout: `app: worker <id> join timeout`
  (ERROR), the task is deleted anyway, a `join_timeouts` counter increments, and
  `on_stop` still runs. Logs `app: worker <id> joined`.
* **Worker → UI:** the worker publishes `STARK_EVT_APP_REQUEST` with
  `app.id = STARK_APP_REQ_NOTIFY` and an app-defined `arg`; it reaches the app's screen
  through the normal bus (no new event family).
* **Crash containment:** `void stark_app_fail(stark_err_t err);` — callable from the UI
  task or the worker. It records the current launch generation and publishes
  `STARK_EVT_APP_REQUEST` `STARK_APP_REQ_FAIL`. `stark_app` subscribes to APP_REQUEST
  itself (it must act even under a dialog). A FAIL whose generation matches the running
  app: log `app: fault <id>: <err>`, `stark_ui_pop_to_root()` (join, `on_stop`,
  `app: stop <id>`), then `stark_ui_dialog_alert("App stopped", "<title>: <err>")`. A
  stale FAIL (other generation, or no app) is ignored with a DEBUG log. The generation
  filter is a pure function (`app_fault.c`, host-tested). A failing `on_start` keeps its
  V0 behaviour (launcher stays, `app: start <id> failed:`).
* **App Test** gains `Worker demo` (worker publishes NOTIFY every 200 ms; screen shows
  the count; logs `apptest: worker n=<k>`), `Fail` (UI-task `stark_app_fail(STARK_ERR_IO)`),
  `Fail from worker`, and `Worker ignores stop` (exercises the timeout path). Stays
  ≤ 150 lines or moves its menu table to a second file.
* Memory: +4 kB static stack; heap gate unchanged.

**Host tests.** `test_app_fault.c`: current-generation fault accepted; stale generation,
no running app and generation wrap rejected. 100 % lines of `app_fault.c`.

**Emulator / host UI port** (ADR-0017). `test/emu/v01-app-lifecycle.toml` on the
production image (the real FreeRTOS worker): Worker demo → `apptest: worker n=1..3` →
BACK → `app: worker apptest joined` before `app: stop apptest`; relaunch → Fail →
`app: fault apptest: I/O error`, `app: stop apptest`, `dialog: open "App stopped"` → OK;
Fail from worker → `app: fault apptest: Timeout`, the worker joined, the same unwinding;
Worker ignores stop → BACK → `app: worker apptest join timeout` then `app: stop apptest`.
`test_ui_fault.c` on the host UI port (a no-worker stand-in for `app_worker.c`): the
fault unwinding and alert, a stray report with no app, a stale-generation report, and
the worker items degrading to `Not supported`. A throwaway emulator leak run: 20 worker
start/stop cycles, About's free heap before and after identical.

**AC.**
1. `v01-app-lifecycle.toml` passes in CI; the log order proves join-before-`on_stop`.
2. No `panic:` or Guru Meditation across the fault paths (runner check).
3. Leak run shows no heap growth; the worker uses no heap.
4. Host tests pass at 100 % of `app_fault.c`.
5. ARCHITECTURE §6.8 (worker, join, fault) and §8 (task table: the worker task) updated.

**Out of scope.** More than one worker per app, preemptive app scheduling, sandboxing,
restarting a crashed app automatically, catching CPU exceptions (a Guru Meditation is
still a reboot — containment covers reported errors, not memory corruption).

**Integration.** STARK-0109 reports `join_timeouts`; V0.2+ workers (SD writes, receive
loops) use this helper.

**CI.** New scenario, new host test. **Hardware.** None. **Security.** Workers cannot
outlive their app (join or delete), so no background activity survives leaving an app —
the basis for SECURITY_SCOPE §4.4 (no unattended loops) in later radio apps.

---

## STARK-0109 — `stark_diag` v1 + Diagnostics app

**Goal.** Heap, task stack high-water marks, FPS, frame overruns and event drops are
measured in one place, logged, and shown live in an app.

**Layer.** L3 (`stark_diag`: pure core + port); composition root wiring; L5 app.

**Files.** `components/stark_diag/**` (new: `diag_core.{c,h}`, `stark_diag.c`,
`include/stark_diag.h`, `Kconfig.stark`, `CMakeLists.txt`), `main/{stark_main.c,Kconfig.projbuild,app_registry.{c,h}}`,
`apps/app_diagnostics/**` (new), `components/stark_app/app_manager.c` (expose
`join_timeouts` via a getter), `scripts/{check_layers.py,test_wokwi.sh}`,
`sdkconfig.defaults` (`CONFIG_FREERTOS_USE_TRACE_FACILITY=y`),
`test/host/{test_diag_core.c,CMakeLists.txt}`, `test/emu/v01-diag.toml`,
ARCHITECTURE.md §2/§6.9/§7/§10, TESTING.md §4.

**Deps.** STARK-0108.

**Notes.**
* `stark_diag` is L3 and may not include `stark_ui`/`stark_app` (L4). Their counters are
  **injected**: `typedef struct { uint32_t frames, overruns, render_errors,
  join_timeouts; } stark_diag_ui_t;` and `stark_err_t stark_diag_init(void (*ui_source)(stark_diag_ui_t *out));`
  — `main` passes an adapter over `stark_ui_stats()` and the app manager's getter.
  `stark_diag → stark_event` is a new sideways L3 edge: add it to the allowlist and
  ARCHITECTURE §10.
* `stark_err_t stark_diag_snapshot(stark_diag_snapshot_t *out)`: free internal heap,
  minimum-ever free (`heap_caps_get_minimum_free_size`), largest free block, FPS × 10
  over the last sampling window, frames, overruns, render errors, join timeouts, events
  published/dropped/max depth (`stark_event_stats()`), uptime s.
  `size_t stark_diag_tasks(stark_diag_task_t *out, size_t max)`: name + stack high-water
  mark in bytes for every task (`uxTaskGetSystemState` into a static array of
  `STARK_DIAG_MAX_TASKS` 16).
* A 1 s `esp_timer` sampler computes FPS from frame-counter deltas (pure
  `diag_fps_x10()`, wrap-safe) and every `CONFIG_STARK_DIAG_LOG_PERIOD_S` (10; 0 = off)
  logs `diag: heap=<n> min=<n> fps=<n.n> drops=<n> overruns=<n>` (format by pure
  `diag_format()`). `heap=` stays first so `main`'s boot `diag: heap=<n>` contract and the
  runner's parser hold.
* `scripts/test_wokwi.sh` adds: every `diag:` line carrying `drops=` must show
  `drops=0` (ROADMAP V0.1 exit 3 over the whole scenario set).
* **Diagnostics app** (`apps/app_diagnostics`, id `diagnostics`, title `Diagnostics`,
  category `System`, registry position second): refreshes once a second (About pattern),
  mono10 rows: heap free/min/largest, FPS, frames, overruns, render errors, join
  timeouts, events published/dropped/peak, uptime, then the task table (name, HWM).
  Redraws only changed rows. Logs `diagnostics: heap=<n> fps=<n.n>` per refresh (the
  values it drew).
* FPS and overruns are **reported**, never gated in simulation (ADR-0011).

**Host tests.** `test_diag_core.c`: FPS for zero elapsed time, a counter wrap, sub-1 fps;
`diag_format()` exact strings including large values and buffer truncation; min-heap
tracking. `diag_core.c` ≥ 95 % lines.

**Host UI port / emulator** (ADR-0017). `test_ui_diagnostics.c` renders the real
Diagnostics screen on the host UI port with a settable fake `stark_diag`: the heap, FPS,
overrun, join-timeout, event and task rows are pixel-identical to direct renders of the
expected text (frame inspected), and a new sample redraws exactly the two rows that
changed. `test/emu/v01-diag.toml` on the production image: Diagnostics refreshes once a
second (`diagnostics: heap=…` three times) and stark_diag's `diag:` line reports
`drops=0` — which the emulator harness now gates in every scenario.

**AC.**
1. Diagnostics shows live heap and FPS (host UI port: rows match the values; emulator:
   live refresh; ROADMAP V0.1 exit 4).
2. `drops=0` gate active in `scripts/test_emu.py` (and the optional `test_wokwi.sh`);
   all scenarios pass under it.
3. `check_layers.py` classifies `stark_diag` as L3 with the one new sideways edge; no
   include of an L4 header in `components/stark_diag/`.
4. Host tests pass at the coverage above.
5. Heap at the root stays ≥ 200 kB (runner gate); the figure is recorded in the PR.
6. ARCHITECTURE §6.9 moves `stark_diag` v1 from "deferred" to a contract (self-test and
   log export stay V0.7); §7 boot sequence shows `stark_diag_init()`.

**Out of scope.** Self-test routines and log export (V0.7), CPU load per task, logging
to storage (V0.2), resetting counters, any network telemetry.

**Integration.** STARK-0110's soak reads these lines.

**CI.** New component, scenario, host test; `sdkconfig.defaults` change.
**Hardware.** None; task HWMs are re-read on hardware at V1. **Security.** Local only —
no telemetry leaves the device.

---

## STARK-0110 — V0.1 soak scenario, measurements, milestone close-out

**Goal.** V0.1's exit criteria are demonstrated in CI and recorded.

**Layer.** Tests, CI, docs.

**Files.** `test/emu/soak/v01-soak.toml` (new), `scripts/test_emu.py`,
`.github/workflows/ci.yml` (`esp-emulator-soak` job), `docs/measurements.md`, `README.md`,
`WOKWI.md` §6, TESTING.md §4.

**Deps.** STARK-0109.

**Notes.**
* `test/emu/soak/v01-soak.toml` (ADR-0017): 60 s of emulated use on the production image,
  cycling through every app — About, Diagnostics, App Test (a confirm and an alert, a
  worker, a contained fault, a long-BACK home), Buzzer Test, Display Test, Input Test,
  Hello — then idle at the launcher so stark_diag's last line (at ~61 s) is taken at the
  root menu. It lives outside the default set and declares `wall_limit_s`.
* Harness checks: `drops=0` on every `diag:` line (always, since STARK-0109) and
  `heap_delta_max = 1024` — the first and last stark_diag heap readings (the silent init
  sample, taken before the main task freed its stack, is not logged).
* CI: an `esp-emulator-soak` job on pull requests and manual dispatch (free; about eight
  minutes of wall time, in parallel). TESTING §4 records the soak as the one budget
  exception.
* `docs/measurements.md` gains V0.1 rows (heap at boot, soak heap delta, drops,
  overruns *reported*, FPS, binary size, font flash cost, worker slot cost, leak run,
  coverage) and a V0.1 exit-criteria evidence table. Hardware table unchanged.
* README status → "V0.1 complete".

**Host tests.** Whole suite green; coverage report attached (pure cores and V0.1
pure helpers).

**Emulator/runtime.** Full scenario set and the soak green in CI (ADR-0017).

**AC.**
1. Every V0.1 exit criterion (ROADMAP) demonstrably met with evidence in
   `docs/measurements.md`.
2. `soak/v01-soak.toml` passes in CI: `drops=0` throughout, heap delta ≤ 1 kB.
3. The hello app still satisfies exit 1 (`wc -l`, `grep` from STARK-0104).
4. `grep -ri wokwi components/ apps/ main/` returns nothing.
5. No scenario asserts a wall-clock value; overruns appear only as reported figures.

**Out of scope.** V0.2 work, hardware measurements.

**CI.** New dispatch-only job. **Hardware.** None; V0.2 is also simulation-first
(ADR-0018: software-verified, physical microSD at V1) — the first milestone needing
hardware halves is V0.3.
**Security.** None.

---

## Infrastructure debt register

Documented gates that are **not yet active**. Each needs its own task before it becomes
a gate; none blocks V0.1.

| Item | Where promised | Why not in V0.1 |
| --- | --- | --- |
| `clang-tidy` (bugprone/cert/readability subset) over `stark_*`, failing CI | TESTING.md §3, §7 | Needs a compile database clang can digest for Xtensa GCC flags, or a host-side run over pure sources only; a scoped decision of its own |
| Binary-size regression flag (±10 %) and size report as a PR comment | TESTING.md §3, §7 | Needs a stored baseline and PR-comment permissions; sizes are recorded manually in `docs/measurements.md` meanwhile |

Resolved while planning V0.1: the `wokwi` job spent simulator minutes on documentation-only
change sets and failed once the monthly quota ran out. It now always runs but invokes
Wokwi only for runtime-relevant changes (`scripts/wokwi_gate.py`, WOKWI.md §6; merged
separately as CI infrastructure, PR #20).

---

# V0.2 — storage & settings

**Scope source:** [ROADMAP.md](ROADMAP.md) "V0.2". Nothing from V0.3+ is pulled
forward (no captures format, no capability bits, no radios, no formatting tool).

**Completion levels (ADR-0018, owner decision 2026-10-23).** *V0.2 complete* means the
storage and settings **software logic is verified** with free, local tiers. It never
means physical microSD behaviour is verified. Every task below lists its **software
evidence (required now)** and its **physical HIL evidence (deferred to V1)**; the
deferred half is collected in ROADMAP V1 exit 8 and TESTING.md §5.

**Validation (ADR-0017/0018).** Tiers used:

* **Host (Tier 1/4A)** — pure cores at 100 % lines where stated; the real POSIX file
  I/O ops running against a host temp directory; `test/host/support/fake_medium.c`
  and a fault-injecting I/O wrapper (fail the Nth call, short writes, `ENOSPC`, `EIO`
  from a byte offset on, failing `fsync`/`rename`/`unlink`/`mkdir`, a "removed" switch);
  a fake NVS store with the same failure switches.
* **Host UI port (Tier 4B)** — the real screens rendered into memory and compared with
  inspected golden frames (docs/VALIDATION.md §3.3).
* **Emulator, production image (Tier 3)** — `test/emu/*.toml` on
  `build/merged-binary.bin`. esp-emu models no SD card, so **every production-image
  boot is a no-card boot**; NVS lives in emulated flash and survives a chip reset over
  the esp-emu control channel.
* **Emulator, storage test image (Tier 4B on the emulator)** — `test/emu/storage/*.toml`
  on `build/emu-storage/merged-binary.bin` (ADR-0018 §3): the production `main`, apps
  and components with `stark_storage_medium` replaced by an internal-flash FAT volume
  whose presence and "formatted" flag the harness flips over GDB. Its log lines are the
  production code's; only the medium differs.

Names: a "scenario" is an emulator scenario; a "golden" is an inspected host render;
"physical, deferred to HIL" marks evidence collected at the V1 prototype.

**Hardware.** None. The pin map is unchanged (`SD_CS` = GPIO 8 is already reserved and
in `stark_board`); no card-detect pin (ADR-0018 §5).

**Exit-criteria map** (ROADMAP V0.2; each row's V1 half is ROADMAP V1 exit 8):

| Exit criterion | V0.2 software evidence | Proven by |
| --- | --- | --- |
| 1. Settings survive reboot | Production image: set in the Settings app → control-channel reset → loaded and applied | STARK-0204 (store, reset harness), STARK-0205 (`v02-settings-persist.toml`) |
| 2. 100 kB write + byte-identical read-back while the UI renders | Storage test image: worker streams 100 kB, CRC-32 and byte compare match, UI renders and moves the selection during the write | STARK-0203 (helpers, write test), STARK-0207 (`storage/v02-write-under-render.toml`) |
| 3. Card removed mid-write degrades gracefully | Storage test image: medium removed mid-write → error surfaced, no panic, remount; host fault injection at every I/O step | STARK-0202 (removal), STARK-0203 (helpers), STARK-0207 (`storage/v02-remove-mid-write.toml`) |
| 4. Mount failure leaves the device usable | Production image (no card): every app reachable, settings work; storage image: unformatted medium; host: mount errors | STARK-0201, STARK-0207 (`v02-no-card.toml`) |

**System event ids** (`STARK_EVT_SYSTEM`, `system.id`; ARCHITECTURE §6.3 keeps the
table): 1 `STARK_SYS_DIAG_SAMPLE` (V0.1), 2 `STARK_SYS_STORAGE` (`arg` = new
`stark_storage_state_t`), 3 `STARK_SYS_SETTING` (`arg` = `stark_setting_t` that changed).

**Launcher.** The two new apps, Storage (STARK-0202) and Settings (STARK-0205), form a
new category **Device**, appended after Examples, so every V0/V0.1 scenario's key
sequence stays valid; only `app: registry n=…` expectations change.

**Planning-debt audit at V0.1 close** (what V0.2 inherits):

| Item | Disposition |
| --- | --- |
| `clang-tidy`, binary-size flag | Stay in the infrastructure debt register; not V0.2 exit criteria |
| `stark_log` promised a file sink "at V0.2 without touching call sites" | STARK-0206 — the facade stays; capture hooks below it |
| Runtime key-repeat adjustment promised for V0.2 (ARCHITECTURE §6.6) | STARK-0204 (`stark_input_set_timing()`), STARK-0205 (setting) |
| Board capability struct promised for V0.2 (ARCHITECTURE §6.1) | Superseded by ADR-0018 §7: `sd_cs != -1` is the capability |
| `sim/diagram.storage.json` (old STARK-0203 title, ROADMAP scope) | Dropped (ADR-0018 §8); the storage test image replaces it |
| Physical SD + shared-bus verification | Deferred to V1 (ADR-0018 §1), listed per task |

---

## STARK-0201 — `stark_storage` v1: medium seam, mount state machine, storage task

**Goal.** A FAT volume on the microSD card is mounted at `/sd` in the background without
delaying boot, its state is tracked by a pure, host-tested state machine and published
as an event, and the same code runs against the emulator's internal-flash medium.

**Layer.** L1 `stark_storage_medium` (hardware port); L3 `stark_storage` (L2 pure core
`storage_core.c` + L3 port `stark_storage.c`); composition root.

**Files.**
* `components/stark_storage_medium/{include/stark_storage_medium.h,medium_sdspi.c,CMakeLists.txt}` (new)
* `components/stark_storage/{include/stark_storage.h,storage_core.{c,h},stark_storage.c,Kconfig.stark,CMakeLists.txt}` (new)
* `main/{stark_main.c,Kconfig.projbuild,CMakeLists.txt}`, `sdkconfig.defaults` (FATFS: long names on heap, `CONFIG_FATFS_MAX_LFN=64`)
* `cmake/stark_flags.cmake` (new: the warning-flag walk moved out of the root `CMakeLists.txt` so both firmware projects share it), `CMakeLists.txt`
* `test/emu/storage_image/{CMakeLists.txt,partitions.csv,dependencies.lock,components/stark_storage_medium/{medium_flashfat.c,CMakeLists.txt}}` (new)
* `scripts/{check_layers.py,check_emu_image.py (new),test_check_emu_image.py (new),test_emu.py,test_emu.sh,build.sh}`, `scripts/test_emu_harness.py`, `components/stark_board/board_devkitc1.c` (SD CS pull-up)
* `.github/workflows/ci.yml` (new `esp-emulator-storage` job)
* `test/host/{test_storage_core.c,CMakeLists.txt}`, `scripts/test_host.sh` (coverage filter)
* `test/emu/v02-storage-boot.toml`, `test/emu/storage/v02-storage-mount.toml`
* ARCHITECTURE.md §2/§6.9/§7/§8/§9/§10, docs/VALIDATION.md §2/§3.5, TESTING.md §4/§7

**Deps.** STARK-0110.

**Notes.**
* **Medium port** (`stark_storage_medium.h`, ADR-0018 §2) — only `stark_storage` calls it,
  always under its mutex:
  ```c
  typedef struct { uint64_t total_bytes, free_bytes; } stark_medium_space_t;
  const char *stark_storage_medium_name(void);                 /* "sdspi" */
  stark_err_t stark_storage_medium_mount(const char *mount_point, size_t max_files);
      /* OK; NOT_FOUND = no card answers; NOT_SUPPORTED = card answers, no FAT volume;
         IO = anything else. Never formats (ADR-0018 §6). */
  void        stark_storage_medium_unmount(void);              /* idempotent */
  stark_err_t stark_storage_medium_probe(void);                /* mounted: CMD13 → OK / NOT_FOUND */
  stark_err_t stark_storage_medium_space(stark_medium_space_t *out);
  /* STARK-0202 adds stark_storage_medium_present() (card init only, for EJECTED). */
  ```
  `medium_sdspi.c` (as built): the card is attached to `SPI2_HOST` once
  (`sdspi_host_init_device`, CS = `stark_board_pins()->sd_cs`) and stays attached, so
  its CS is driven high between uses and retries do not attach/detach the device; each
  mount attempt runs `sdmmc_card_init()` (clock ceiling `CONFIG_STARK_STORAGE_SD_KHZ`,
  20000, range 400–20000) and mounts with `esp_vfs_fat_register()` + `f_mount()` —
  never formatting; `sdmmc_get_status()` for `probe`; `esp_vfs_fat_info()` for space.
  `stark_board_init()` pulls `SD_CS` up before the first attach. The SD/FAT drivers'
  own per-attempt ERROR lines are switched off (one STARK line per state change
  instead). No STARK code other
  than this file and `stark_display` issues SPI2 transactions (ADR-0018 §4).
* **States** (`stark_storage_state_t`): `NOT_FITTED` (board `sd_cs == -1`; nothing runs),
  `ABSENT`, `MOUNTED`, `UNUSABLE` (card answers, no FAT), `REMOVED` (lost while mounted),
  `EJECTED` (STARK-0202). The pure core decides every transition and the next action
  from `(state, input, now_ms)`: inputs `MOUNT_RESULT(err)`, `PROBE_RESULT(err)`,
  `IO_ERROR`, `TICK`, `REQ_MOUNT`, `REQ_EJECT` (the last two used from STARK-0202);
  actions `TRY_MOUNT`, `PROBE`, `PRESENT_CHECK`, `UNMOUNT`, `NONE`, plus a "publish"
  flag on every state change. Retry while `ABSENT`/`REMOVED`/`UNUSABLE` every
  `CONFIG_STARK_STORAGE_RETRY_MS` (3000, range 1000–60000); probe while `MOUNTED` every
  `CONFIG_STARK_STORAGE_PROBE_MS` (1000, range 250–10000); an `IO_ERROR` while mounted
  schedules an immediate probe. No libc, no ESP-IDF in the core.
* **Port** (`stark_storage.c`): `stark_err_t stark_storage_init(void)` — static mutex,
  static task `stark_storage` (prio `CONFIG_STARK_STORAGE_TASK_PRIO` 2, stack
  `CONFIG_STARK_STORAGE_TASK_STACK` 4096, core 0); returns immediately (the first mount
  attempt runs in the task, so `boot: ui_ready` never waits for a card).
  `stark_storage_state_t stark_storage_state(void)` (any task, atomic).
  `stark_err_t stark_storage_space(stark_medium_space_t *out)` (`STARK_ERR_STATE` unless
  mounted). On every state change: publish `STARK_EVT_SYSTEM`/`STARK_SYS_STORAGE` and
  log exactly one line (contract): `storage: not fitted`, `storage: no card`,
  `storage: mounted <medium> total=<kB> free=<kB>`, `storage: unusable card: <err>`,
  `storage: card removed` (WARN). Retries that do not change the state are silent.
* **Boot** (ARCHITECTURE §7): `stark_storage_init()` after `stark_diag_init()` — last, so
  a slow or stuck medium can never hold up the UI.
* **Storage test image** (ADR-0018 §3): `test/emu/storage_image/CMakeLists.txt` lists the
  repository's `main`, every `apps/app_*` and every `components/*` except
  `stark_storage_medium` (explicit filter, so the substitution is visible in one line),
  plus its own `components/stark_storage_medium`; includes `cmake/stark_flags.cmake`;
  `SDKCONFIG_DEFAULTS` = the repository's `sdkconfig.defaults` unchanged; the
  production setting `CONFIG_PARTITION_TABLE_FILENAME="partitions.csv"` resolves to the
  test project's own `partitions.csv` (the production table plus `emufat, data, fat, ,
  1M`), so the resolved sdkconfigs are identical. `medium_flashfat.c` mounts that partition at the mount point with
  `esp_vfs_fat_spiflash_mount_rw_wl()` (formatting it once if it has never been
  formatted — the test medium's own blank flash, not user media) and exposes
  `volatile uint8_t emu_medium_present = 1, emu_medium_formatted = 1;`. While
  `present == 0`: `mount` → `NOT_FOUND`, `probe`/`present` → `NOT_FOUND`, and every
  sector read/write through its wrapped wear-levelling diskio fails as a pulled card's
  would; while `formatted == 0`: `mount` → `NOT_SUPPORTED`. Name: `"emu-flashfat"`.
* `scripts/build.sh --docker --emu-storage` builds it to `build/emu-storage/` (+ merged
  image). `scripts/check_emu_image.py` compares the two builds'
  `project_description.json` component lists (sets, directories, sources, include
  directories, requirements) and `config/sdkconfig.json`: they must differ in exactly
  `stark_storage_medium`'s directory, the sdkconfigs must be identical, and the test
  partition table must start with the production one; anything else fails.
  Unit-tested in `scripts/test_check_emu_image.py`.
* **Harness:** a scenario key `image = "storage"` selects `build/emu-storage/*`; the
  default set (`test/emu/*.toml`) stays production-only and the runner refuses a
  scenario whose `image` does not match the firmware it was given.
* `check_layers.py`: `stark_storage_medium` L1; `stark_storage` L3 with the sideways edge
  `stark_storage → stark_event`; `storage_core.c`'s purity is enforced by its host
  build (no ESP-IDF on the include path). ARCHITECTURE §10 table updated in the same
  change.

**Host tests.** `test_storage_core.c`: every state × input transition, retry and probe
cadence across `now_ms` wrap, no publish without a state change, `IO_ERROR` while not
mounted ignored, `NOT_FITTED` absorbing. `storage_core.c` 100 % lines. `fake_medium.c`
drives the port's mount/probe sequencing in a host build of `stark_storage.c` with a fake
mutex/task shim only if that shim stays under 150 lines; otherwise the port is covered by
the emulator scenarios and only the core is host-tested (record which in the PR). As
built: the core only — the port is a thin action loop, covered by both images'
scenarios.

**Emulator.** `v02-storage-boot.toml` (production image, no card): `boot: ui_ready` then
`storage: no card`; a key press after it still reaches the launcher (`menu: sel=1`);
`heap_min = 200000`; no `storage: mounted`. `storage/v02-storage-mount.toml` (storage
image): `storage: mounted emu-flashfat total=` after `boot: ui_ready`.

**Fault injection.** Host: every `mount` result (`OK`/`NOT_FOUND`/`NOT_SUPPORTED`/`IO`)
and every `probe` result in every state. Emulator: the production image's no-card boot
(every boot); GDB-driven removal arrives with STARK-0202, the unusable-card scenario
with STARK-0207.

**AC.**
1. Production-image boot logs `storage: no card` after `boot: ui_ready`; the device
   stays navigable (`v02-storage-boot.toml` in CI).
2. Storage image mounts its medium (`storage/v02-storage-mount.toml` in CI, new
   `esp-emulator-storage` job).
3. `check_emu_image.py` passes in that job: the images differ only in
   `stark_storage_medium` and the partition-table keys.
4. `storage_core.c` at 100 % lines; whole host suite green on Linux and macOS.
5. `check_layers.py` classifies both components; `grep -rIiE 'wokwi|qemu|esp_emu|emu_medium'
   components/ apps/ main/` is empty.
6. Root heap ≥ 200 kB on the production image (runner gate); the heap and flash cost of
   FATFS + SDSPI is recorded in the PR.

**Out of scope.** Eject, removal handling beyond the state machine (STARK-0202), file
helpers and paths (STARK-0203), formatting, SDMMC 4-bit, a card-detect pin, an
internal-flash fallback store in production (ADR-0007).

**Resource limits.** Heap after mount ≤ 12 kB more than V0.1 at the root (FATFS volume,
sector cache, SDSPI host); storage task stack HWM ≥ 1 kB in the emulator; flash
≤ +80 kB.

**CI.** New component pair, new host test, two scenarios, a new required job
`esp-emulator-storage` (build the storage image, run `check_emu_image.py`, run
`test/emu/storage/*.toml`), `build.sh` flag. **Security.** No auto-format: the firmware
never destroys data on a card it cannot read. **Physical HIL (deferred to V1).** Mount
of a real card at 20 MHz on the shared bus; no-card and wrong-card boots on the
breadboard; init time of a real card.

---

## STARK-0202 — Removal and reinsertion, safe eject, status bar, Storage app

**Goal.** Losing the card never panics: removal is detected, surfaced to the user and
recovered from on reinsertion; the user can eject safely; the status bar shows the card
state; a Storage app shows status and offers Eject / Mount.

**Layer.** L3 `stark_storage`; L4 `stark_ui` (status bar); composition root; L5 app.

**Files.** `components/stark_storage/{storage_core.{c,h},stark_storage.c,include/stark_storage.h}`,
`components/stark_ui/{ui_statusbar.c,include/stark_ui.h,include/stark_theme.h}`,
`main/{stark_main.c,storage_ui.{c,h} (new),app_registry.{c,h}}`, `apps/app_storage/**` (new),
`scripts/test_emu.py` (GDB `pokes`), `scripts/test_emu_harness.py`,
`test/host/{test_storage_core.c,test_ui_statusbar.c,test_ui_storage.c,CMakeLists.txt}`,
`test/emu/storage/v02-remove-insert.toml`, `test/emu/storage/v02-eject.toml`,
existing scenarios' `app: registry` lines (and the Hello/soak key paths: Storage is now
the launcher's last row), `test/host/port/ui_port_storage.{c,h}`, ARCHITECTURE
§6.7/§6.9b, TESTING §4, docs/VALIDATION.md §2.

**Deps.** STARK-0201.

**Notes.**
* **Removal** (both paths converge on one core transition): (a) a probe returns
  `NOT_FOUND`/`IO`; (b) a file operation fails with an I/O error (STARK-0203 reports it
  via `IO_ERROR`) and the immediate probe confirms. Then, under the mutex: the volume is
  unmounted, the mount generation increments, state → `REMOVED`, event published,
  `storage: card removed`. (As built: handles do not exist before STARK-0203, which
  closes them at the VFS level and marks them stale on every generation bump.) Retries resume at the retry cadence; a successful mount → `MOUNTED`.
* **Eject** — `stark_err_t stark_storage_request_eject(void)` and
  `stark_storage_request_mount(void)`: non-blocking requests handled by the storage task
  (safe from the UI task). Eject with open handles (other than the log writer's) is
  refused: `storage: eject refused: <n> open`, event re-published with the unchanged
  state so the UI can say so. Otherwise: flush/close internal files, unmount,
  `storage: ejected`, state `EJECTED`. While `EJECTED` the task runs `present()` at the
  retry cadence and never mounts: a card that stops answering moves to `ABSENT` (the
  user pulled it), so the next answer mounts it again; `REQ_MOUNT` mounts at once.
* **Status bar** — `void stark_ui_status_set_storage(stark_ui_storage_icon_t icon)`
  (`NONE`, `ABSENT`, `MOUNTED`, `ERROR`, `EJECTED`), an 8 × 10 card glyph at the right of
  the status bar, colours from `stark_theme.h`; damages only its own rectangle. `NONE`
  draws nothing (V0.1 status bar pixel-identical).
* **Composition root** (`main/storage_ui.c`): subscribes to `STARK_SYS_STORAGE`, maps
  state → icon (`NOT_FITTED`→`NONE`, `ABSENT`→`ABSENT`, `MOUNTED`→`MOUNTED`,
  `REMOVED`/`UNUSABLE`→`ERROR`, `EJECTED`→`EJECTED`) and, on `MOUNTED → REMOVED` only,
  opens `stark_ui_dialog_alert("SD card removed", "Data being written may be
  incomplete.")` (if a dialog is already open, the icon alone surfaces it). Lives in
  `main` because `stark_storage` (L3) may not call `stark_ui` (L4).
* **Storage app** (`apps/app_storage`, id `storage`, title `Storage`, category `Device`,
  registry position after `hello`): a status row (`No card`, `Mounted · <free> free of
  <total>`, `Card removed`, `Ejected`, `Card not usable`, `Not fitted`) refreshed on
  `STARK_SYS_STORAGE`; items `Eject` (confirm dialog "Eject card?"; when not mounted it
  only logs `storageapp: eject: not mounted`) and `Mount` (always a request; the core
  ignores it while mounted). Logs `storageapp: status <state>`. As built: the mounted
  status shows `stark_storage_space()`, which caches the figures at mount so the UI task
  never touches the card.
  ≤ 150 lines per file.
* **Harness** — `pokes = [ { at_ms = 2500, var = "emu_medium_present", value = 0 } ]`:
  applied at the existing core-0 breakpoint when `now_ms` first reaches `at_ms`
  (a global `set var`; the GPIO limitation does not apply to RAM). Valid only with
  `image = "storage"`; the runner rejects pokes on the production image.

**Host tests.** `test_storage_core.c` extended: removal from probe and from `IO_ERROR`,
generation bump, eject refused/accepted, `EJECTED → ABSENT → MOUNTED`, `REQ_MOUNT` in
each state; 100 % lines kept. `test_ui_storage.c` (host UI port with a settable fake
`stark_storage`; as built, one file for both): each glyph state pixel-identical to its
bitmap, `NONE` identical to the V0.1 status bar, only the glyph rectangle damaged on
change; the bridge's alert only on `MOUNTED → REMOVED`; each status text, the eject
confirm/cancel, the mount request; the mounted frame pinned after inspection.

**Emulator** (storage image). `v02-remove-insert.toml`: mounted → poke `present = 0` →
`storage: card removed`, `dialog: open "SD card removed"`, OK → poke `present = 1` →
`storage: mounted emu-flashfat`; no panic. `v02-eject.toml`: Storage app → `Eject` →
confirm → `storage: ejected` (no remount while the card still "answers") → poke
`present = 0` → poke `present = 1` → `storage: mounted`; and `Mount` after an eject
mounts at once.

**Fault injection.** Host: probe failure kinds, `IO_ERROR` during eject, eject while a
handle is open, unmount while handles are open (all closed, all stale). Emulator: GDB
removal and reinsertion at arbitrary times, including during the dialog.

**AC.**
1. Both scenarios pass in CI; removal never produces `panic:`/Guru Meditation.
2. The status bar shows each state (goldens inspected); V0.1 goldens unchanged.
3. Eject with an open file is refused and says so; eject never loses the log writer's
   buffered lines (flushed first — verified in STARK-0206 once the writer exists; here
   the hook is a no-op).
4. Host coverage: `storage_core.c` 100 %.
5. Existing scenarios still pass with only their `app: registry` lines updated.

**Out of scope.** File helpers (STARK-0203), the write test (STARK-0203), a card-detect
pin, reporting which file was being written when the card went.

**Resource limits.** Storage app ≤ 2 kB static RAM; no heap growth across 10
remove/insert cycles (emulator, root heap equal ± 256 B — card buffers are re-used).

**CI.** Two scenarios in `esp-emulator-storage`, three host tests, harness `pokes` with
unit tests. **Security/safety.** Eject is the documented safe way to remove a card
(ADR-0007); the alert tells the user unflushed data may be lost — it never claims
success it did not verify. **Physical HIL (deferred to V1).** Real removal and
reinsertion, detection latency with the CMD13 poll, behaviour of cards that answer
slowly after insertion, MISO tri-state on the shared bus when the SD CS is high.

---
## STARK-0203 — Paths, file helpers, atomic writes, Storage write test

**Goal.** Apps read and write files under `/sd/stark/{captures,logs,apps,config}`
through small helpers that validate names, stream data, write atomically, map every
failure to a `stark_err_t`, and never touch a card that has gone; the Storage app's
100 kB write test exercises them while the UI renders.

**Layer.** L3 `stark_storage` (pure helpers + POSIX I/O ops); `stark_err`; L5 app.

**Files.** `components/stark_storage/{storage_path.{c,h},storage_file.{c,h},storage_io.h,storage_io_posix.c,stark_storage.c,include/stark_storage.h}`,
`components/stark_err/{include/stark_err.h,stark_err.c}` (append `STARK_ERR_NO_SPACE`),
`apps/app_storage/**` (write test, worker), `scripts/test_emu.py` (`expect_re`),
`test/host/{test_storage_path.c,test_storage_file.c,support/fault_io.{c,h},test_err.c,CMakeLists.txt}`,
`test/emu/storage/v02-files.toml`, ARCHITECTURE §5/§6.9, TESTING §4.

**Deps.** STARK-0202.

**As built.** `components/stark_app/app_worker.c` changed too: a worker that has already
returned is reaped by the next `stark_app_worker_start()` (it used to hold the slot until
the app stopped, so the write test could run only once per launch). Host tests share
`test_ui_storage.c` for the write-test item and the CRC-32 check value; the UI port fake
gained stub file calls.

**Notes.**
* **Paths** (pure, `storage_path.c`): `typedef enum { STARK_DIR_CAPTURES, STARK_DIR_LOGS,
  STARK_DIR_APPS, STARK_DIR_CONFIG } stark_dir_t;` `stark_err_t stark_storage_path(stark_dir_t
  dir, const char *name, char *out, size_t out_len)` → `/sd/stark/<dir>/<name>`. A name is
  1–32 characters of `[A-Za-z0-9._-]`, does not start with `.`, does not end in `.tmp`
  (reserved): anything else is `STARK_ERR_INVALID_ARG` — no `/`, no `..`, no traversal.
  `out` too small → `STARK_ERR_INVALID_ARG`.
  On every mount the port creates the four directories (`mkdir`, `EEXIST` fine) and
  deletes stale `*.tmp` files in them: `storage: swept <n> tmp` when n > 0.
* **I/O ops** (`storage_io.h`): a table of `open/read/write/fsync/close/rename/unlink/mkdir/stat/opendir/readdir/closedir`
  wrappers. `storage_io_posix.c` implements it with POSIX calls and compiles unchanged on
  the host (real temp directory) and on the target (VFS → FATFS). The helpers take the
  table at init (production: POSIX; host tests: POSIX or `fault_io` wrapping it). This is
  a test seam *inside* the component, not a second storage path: the target always uses
  POSIX.
* **Handles** (`storage_file.c`): a static pool of `STORAGE_FILE_SLOTS` (4; as built the
  STARK-0201 Kconfig option `STARK_STORAGE_MAX_FILES` was folded into this constant — the
  VFS gets the same number).
  ```c
  typedef enum { STARK_FILE_READ, STARK_FILE_WRITE_ATOMIC, STARK_FILE_APPEND } stark_file_mode_t;
  typedef struct stark_file stark_file_t;
  stark_err_t stark_storage_open(stark_dir_t dir, const char *name, stark_file_mode_t mode,
                                 stark_file_t **out);
  stark_err_t stark_storage_read(stark_file_t *f, void *buf, size_t len, size_t *got);
  stark_err_t stark_storage_write(stark_file_t *f, const void *buf, size_t len);
  stark_err_t stark_storage_close(stark_file_t *f);   /* atomic: fsync, unlink old, rename */
  void        stark_storage_discard(stark_file_t *f); /* atomic: drop the temp file */
  stark_err_t stark_storage_remove(stark_dir_t dir, const char *name);
  stark_err_t stark_storage_list(stark_dir_t dir,
                                 bool (*fn)(const char *name, uint32_t size, void *ctx), void *ctx);
  ```
  `WRITE_ATOMIC` writes `<name>.tmp` and on `close` fsyncs, removes `<name>` and renames
  (FAT cannot rename over a file; the window between unlink and rename is documented —
  ADR-0007: FAT has no power-loss atomicity; the sweep removes orphaned temps; the old
  content is lost only if power fails inside that window). Each call takes the storage
  mutex for that call only — never across calls — so probes, eject and the log writer
  interleave with long streams. A handle from an older mount generation returns
  `STARK_ERR_IO` without touching the VFS; `close` on it frees the slot.
* **Error mapping** (one pure function, host-tested): `ENOSPC` → `STARK_ERR_NO_SPACE`
  (new, appended to `stark_err_t`; string "No space"); `ENOENT` → `NOT_FOUND`; `EEXIST`
  → `STARK_ERR_STATE`; `EINVAL`/`ENAMETOOLONG` → `INVALID_ARG`; anything
  else → `IO`. Every `IO` result while mounted is reported to the core as `IO_ERROR`.
  Not mounted → `STARK_ERR_STATE`; pool exhausted → `STARK_ERR_BUSY`.
* **Calling context:** the helpers block on the card, so they are called from an app
  worker (STARK-0108) or the storage task — never from a UI-task callback (ARCHITECTURE
  §6.8's 16 ms rule). `stark_storage_state()`/requests stay UI-safe.
* **Write test** (Storage app item `Write test`, enabled when mounted): the app's worker
  streams 100 kB (`CONFIG` none — fixed 102 400 B) of a deterministic pattern
  (xorshift32 from a fixed seed) in 4 kB chunks to `captures/wtest.bin` with
  `WRITE_ATOMIC`, then reads it back in 4 kB chunks comparing every byte with the
  regenerated pattern and computing CRC-32 of both. Progress (percent) reaches the screen
  through `STARK_APP_REQ_NOTIFY` every chunk. Logs (contract):
  `storageapp: wtest start`, `storageapp: wtest ok bytes=102400 crc=<8 hex> frames=<n>`
  (`frames` = UI frames rendered between start and end, from `stark_ui_stats()`), or
  `storageapp: wtest failed: <err> at <offset>`; `storageapp: wtest refused: <state>`
  when not mounted. Stopping the app joins the worker, which discards the temp file.
* **Harness** — `expect_re`: ordered regular expressions checked after `expect`, used
  for counts that must be non-zero (e.g. `frames=[1-9]`) — never for wall-clock values.

**Host tests.** `test_storage_path.c`: valid names, every rejection class, exact
output, truncation; 100 % lines. `test_storage_file.c` on the POSIX ops in a temp
directory: write/read round trip, atomic replace (old content until `close`), discard,
append, list, remove, pool exhaustion, stale-generation handles. With `fault_io`: `EIO`
at every call position of a 100 kB atomic write (open, each write, fsync, unlink,
rename, close) — each returns an error, leaves no handle allocated and reports
`IO_ERROR` exactly once; short writes are completed or reported; `ENOSPC` → `NO_SPACE`
with the temp file removed when possible; the "removed" switch makes every op `IO`.
`storage_file.c` ≥ 95 % lines; `test_err.c` covers the new code and string.

**Emulator** (storage image). `v02-files.toml`: Storage app → `Write test` →
`storageapp: wtest start` … `storageapp: wtest ok bytes=102400 crc=` (regex: non-zero
`frames=`); a second run replaces the file atomically; `storage: swept` does not appear
(no orphans). Production image: `Write test` disabled, selecting it logs
`storageapp: wtest refused: no card` (added to `v02-storage-boot.toml`).

**AC.**
1. `v02-files.toml` passes in CI: 100 kB written and read back byte-identical (byte
   compare and CRC-32) on the storage image, with UI frames rendered during the test.
2. Every injected I/O fault returns an error without a crash, leaks no handle and leaves
   no file but an orphaned `.tmp` that the next mount sweeps (host).
3. Path validation rejects traversal and reserved names (host, 100 %).
4. `STARK_ERR_NO_SPACE` appended without renumbering; `stark_err_str` covers it.
5. ARCHITECTURE §6.9 holds the `stark_storage` contract (moved from "deferred").

**Out of scope.** Capture file formats (V0.3+), directory trees below the four roots, a
file browser, file copy/move, timestamps (no RTC), encryption.

**Resource limits.** Handle pool ≤ 4 slots; helpers allocate nothing per call beyond
what newlib/FATFS allocate on `open` (released on `close`); write-test worker stack fits
the 4 kB worker slot (HWM ≥ 512 B, Diagnostics task table).

**CI.** Host tests, one storage-image scenario, harness `expect_re` with unit tests.
**Security.** Name validation closes path traversal for every future app (captures from
radios land here); nothing outside `/sd/stark/` is writable through the helpers.
**Physical HIL (deferred to V1).** The same write test on a real card, timing per 4 kB
chunk, and the worst display band-flush stall while it runs (logic analyser on SPI2).

---

## STARK-0204 — `stark_settings` over NVS: typed keys, defaults, change events, runtime appliers

**Goal.** Brightness, volume, key repeat and log level are typed, validated settings
persisted in NVS, loaded at boot with safe fallbacks, published on change and applied
live; the emulator proves they survive a chip reset.

**Layer.** L3 `stark_settings` (L2 pure core `settings_core.c` + L3 NVS port); L3
`stark_input`, `stark_buzzer` (runtime setters); L1 `stark_log`; composition root.

**Files.** `components/stark_settings/{include/stark_settings.h,settings_core.{c,h},stark_settings.c,CMakeLists.txt}` (new),
`components/stark_input/{input_core.{c,h},input_service.c,include/stark_input.h}` (`input_core_set_timing`, `stark_input_set_timing`),
`components/stark_buzzer/{buzzer.c,buzzer_volume.{c,h} (new, pure),include/stark_buzzer.h}`,
`main/{stark_main.c,settings_apply.{c,h} (new)}`, `scripts/{check_layers.py,test_emu.py,test_emu_harness.py}`,
`test/host/{test_settings_core.c,support/fake_nvs.{c,h},test_input_core.c,test_buzzer_volume.c,CMakeLists.txt}`,
`test/emu/v02-settings-store.toml`, ARCHITECTURE §6.6/§6.6a/§6.9/§7/§10, docs/VALIDATION.md §2.

**Deps.** STARK-0201 (only for the event-id table and boot order; no storage use).

**Notes.**
* **Keys** (the pure core's table; NVS namespace `stark`, values `u8`):

  | `stark_setting_t` | NVS key | Range | Default | Labels / unit |
  | --- | --- | --- | --- | --- |
  | `STARK_SET_BRIGHTNESS` | `bright` | 10–100, step 10 | 100 (V0.1 boot backlight) | % |
  | `STARK_SET_VOLUME` | `volume` | 0–3 | 3 (V0.1 duty) | Off, Low, Medium, High |
  | `STARK_SET_REPEAT` | `repeat` | 0–2 | 1 | Slow (600/180 ms), Normal (Kconfig, 400/120), Fast (250/60) |
  | `STARK_SET_LOG_LEVEL` | `loglvl` | 0–3 | 2 | Error, Warn, Info, Debug |

  plus `schema` = 1. The table is the only place a key is defined.
* **API:**
  ```c
  stark_err_t stark_settings_init(void);                    /* never fails the boot */
  uint8_t     stark_settings_get(stark_setting_t key);      /* any task */
  stark_err_t stark_settings_set(stark_setting_t key, uint8_t value); /* UI task */
  stark_err_t stark_settings_commit(void);                  /* writes dirty keys */
  stark_err_t stark_settings_restore_defaults(void);        /* set all + commit */
  const stark_setting_desc_t *stark_settings_desc(stark_setting_t key);
  ```
  `set` validates (`INVALID_ARG` out of range or off-step), updates RAM, marks dirty and,
  only if the value changed, publishes `STARK_SYS_SETTING` and logs
  `settings: <name>=<value>`. `commit` writes dirty keys and `nvs_commit`s:
  `settings: committed n=<k>` (nothing dirty → no write, no log).
* **Load** (`init`): `nvs_flash_init()`; on `ESP_ERR_NVS_NO_FREE_PAGES` /
  `ESP_ERR_NVS_NEW_VERSION_FOUND` erase and re-init (IDF's documented recovery; NVS holds
  only settings) with `settings: store reset (<reason>)` WARN. Missing key → default;
  out-of-range or off-step value → default, `settings: <name> invalid (<v>), default`
  WARN, rewritten at the next commit. A fresh store (no `schema`) writes `schema=1` and
  commits. Store unavailable (`nvs_open` fails) → RAM-only: defaults, `settings: store
  unavailable: <err>` WARN, `set` works, `commit` returns `STARK_ERR_IO`. Then exactly one
  line: `settings: loaded brightness=<v> volume=<v> repeat=<v> loglvl=<v> (<nvs|defaults|ram>)`.
* The core is pure and takes an injected store (`get_u8`/`set_u8`/`commit` returning
  `stark_err_t`); the port maps `nvs_*` onto it. Host tests run the core against
  `fake_nvs` with failure switches.
* **Appliers** (`main/settings_apply.c` — the composition root, because L3 services may
  not call each other): at boot after the services start and on every
  `STARK_SYS_SETTING`: brightness → `stark_display_set_backlight()`; volume →
  `stark_buzzer_set_volume()`; repeat → `stark_input_set_timing()`; log level →
  `stark_log_set_level("*", …)`, with the `key` tag kept at DEBUG for Info/Debug and set
  to the same level for Warn/Error (TESTING §4 contract unaffected at the default).
  Logs `settings: applied <name>=<value>`.
* `stark_err_t stark_input_set_timing(uint16_t repeat_delay_ms, uint16_t repeat_interval_ms)`
  validates with the core's existing rule, hands the value to the sampler under a
  spinlock, and the sampler applies it before its next `input_core_update()` through the
  new pure `input_core_set_timing()` — key state is kept, the next repeat uses the new
  interval.
* `stark_err_t stark_buzzer_set_volume(uint8_t level)`: 0 mutes (requests succeed
  silently), 1/2/3 → duty `buzzer_volume_duty()` = 5 / 15 / 50 % (pure, host-tested;
  loudness is a V1 measurement).
* **Harness — reset phases:** `resets = [ { at_ms = 2500 } ]` sends `reset` on the esp-emu
  `--control-tcp` channel (flash persists inside the process); presses and pokes take an
  optional `phase` (0 before the first reset, 1 after, …) and the next phase begins when
  emulated time restarts. `end_ms` applies to the last phase. If the 0.44.0 GDB stub
  does not survive a chip reset, phases run as successive esp-emu processes over one
  copied flash image with `--save-state`; either way record the mechanism in
  docs/VALIDATION.md §2 with the evidence. **As built:** the stub does not survive, so the
  key is `control = [ { at_ms, command, phase } ]`: a `"reset"` ends the phase and the next
  phase is a new run over the saved image; any other command (`erase-region …`) goes to
  the control channel, fire-and-forget.
* Boot order (ARCHITECTURE §7): `stark_settings_init()` right after `stark_event_init()`
  (log level takes effect early); appliers run after `stark_input_start()`.
* `check_layers.py`: `stark_settings` L3, sideways edge `stark_settings → stark_event`;
  `settings_core.c` pure.

**Host tests.** `test_settings_core.c`: defaults on a fresh store and `schema` written;
round trip; each invalid value class replaced and rewritten; `set` range/step rejection;
no event/log when unchanged; commit writes only dirty keys; restore defaults; every
`fake_nvs` failure (open, get type mismatch, set, commit) → RAM-only or `IO` exactly as
specified. 100 % lines of `settings_core.c`. `test_input_core.c`: timing change mid-hold.
`test_buzzer_volume.c`: duty table and mute.

**Emulator** (production image). `v02-settings-store.toml`: phase 0 on blank NVS →
`settings: loaded … (defaults)`; reset; phase 1 → `settings: loaded … (nvs)`;
`settings: applied` lines present; no `store unavailable`.

**Fault injection.** Host: every store failure above, corrupted values, a schema newer
than 1 (values still validated). Emulator: `erase-region` of the NVS partition over the
control channel between phases → next boot `(defaults)` again (proves recovery from a
wiped store).

**AC.**
1. `v02-settings-store.toml` passes in CI, including the erase-region recovery.
2. `settings_core.c` 100 % lines; input/buzzer additions covered.
3. No NVS failure can stop the boot (host, every failure switch).
4. Applied values reach their services (`settings: applied` lines; backlight/buzzer
   levels themselves are physical, deferred).
5. ARCHITECTURE §6.9 moves `stark_settings` to a contract; §6.6/§6.6a gain the setters.

**Out of scope.** The Settings app (STARK-0205), per-app settings, string/blob settings,
settings import/export to SD, factory reset of anything but these keys.

**Resource limits.** RAM ≤ 256 B static; NVS writes only on `commit` (no write per key
press); flash ≤ +24 kB (NVS is already linked by the PHY/IDF — measure).

**CI.** New component, host tests, harness `resets`, one scenario. **Security.** No
secrets in NVS; NVS encryption stays off (nothing sensitive stored) — recorded.
**Physical HIL (deferred to V1).** Survival across a real power cycle; backlight levels
visibly distinct; buzzer volume steps audible.

---

## STARK-0205 — Settings app

**Goal.** The user changes brightness, volume, key repeat and log level, sees each
change take effect immediately, restores defaults with a confirmation, and the values
survive a reboot.

**Layer.** L4 `stark_ui` (menu value column); L5 app; composition root (registry).

**Files.** `components/stark_ui/{ui_menu.c,include/ui_menu.h}` (optional value text per
item, LEFT/RIGHT hook), `apps/app_settings/**` (new), `main/app_registry.{c,h}`,
`test/host/{test_ui_settings.c,test_ui_launcher.c,CMakeLists.txt}`,
`test/emu/v02-settings-persist.toml`, existing scenarios' `app: registry` lines,
ARCHITECTURE §6.7, TESTING §4.

**Deps.** STARK-0204, STARK-0202 (registry/category placement).

**Notes.**
* `ui_menu` gains `const char *(*value)(size_t index, void *ctx)` (right-aligned value text,
  NULL = none) and `bool (*adjust)(size_t index, int dir, void *ctx)` for LEFT/RIGHT
  (returns true when the value changed → the row is damaged). Menus without them render
  pixel-identically to V0.1 (launcher goldens unchanged).
* **App** (id `settings`, title `Settings`, category `Device`, registry after `storage`):
  rows `Brightness 100%`, `Volume High`, `Key repeat Normal`, `Log level Info`,
  `Restore defaults`. LEFT/RIGHT (with key repeat) step a value → `stark_settings_set()`
  → applied live by the composition root; OK on a value row steps forward with wrap.
  `Restore defaults` → `stark_ui_dialog_confirm("Restore defaults?", …)` → OK →
  `stark_settings_restore_defaults()`. Leaving the app (`on_stop`: BACK, long-BACK,
  fault) calls `stark_settings_commit()`. ≤ 150 lines per file.
* Changing the log level to Error hides INFO scenario lines by design; the scenarios
  never leave it there.

**Host tests.** `test_ui_settings.c` on the host UI port with the real `stark_settings`
core over `fake_nvs`: each row's text and value pixel-identical to inspected goldens;
LEFT/RIGHT clamps at range ends; OK wraps; restore-defaults confirm → defaults; BACK
commits (fake store shows the writes). `test_ui_launcher.c`: Device category rows.

**Emulator** (production image). `v02-settings-persist.toml`: Settings → brightness
down twice (`settings: brightness=80`), volume down once (`settings: volume=2`) → BACK
(`settings: committed n=2`) → reset → phase 1 `settings: loaded brightness=80 volume=2
repeat=1 loglvl=2 (nvs)`, `settings: applied brightness=80`; then Settings → Restore
defaults → OK → BACK → reset → `settings: loaded brightness=100 volume=3 … (nvs)`.

**AC.**
1. `v02-settings-persist.toml` passes in CI (ROADMAP V0.2 exit 1, software half).
2. Goldens inspected; V0.1 launcher/menu goldens unchanged.
3. App files ≤ 150 lines each; no edit under `components/` except the `ui_menu`
   extension named here.

**As built.** Settings sorts before Storage in the Device category, so the launcher rows
become Device header 10, Settings 11, Storage 12; `v01-hello` (three UPs from About) and the
V0.1 soak (three DOWNs after each Hello visit) follow, as do the `sel=12 "Storage"` lines.
The host test uses a UI-port `stark_settings` over the real core and the fake NVS
(`test/host/port/ui_port_settings.c`); the opening frame is pinned after inspection.

**Out of scope.** Wi-Fi/BT/time settings, per-app settings pages, a slider widget.

**Resource limits.** App static RAM ≤ 1 kB; no heap.

**CI.** Host test, scenario. **Security.** None. **Physical HIL (deferred to V1).**
Brightness/volume perceptible on the breadboard; repeat presets feel right.

---

## STARK-0206 — Rotating file log sink for `stark_log`

**Goal.** Console log lines are also kept, bounded, on the card in
`/sd/stark/logs/`, rotating, without ever blocking the task that logged, and without
changing a single `STARK_LOG*` call site.

**Layer.** L1 `stark_log` (capture ring, pure core `log_ring.c`); L3 `stark_storage`
(writer + pure rotation plan `log_rotate.c`).

**Files.** `components/stark_log/{stark_log.c,log_ring.{c,h} (new),include/stark_log.h,Kconfig.stark (new),CMakeLists.txt}`,
`components/stark_storage/{storage_log.c (new),log_rotate.{c,h} (new),stark_storage.c,Kconfig.stark}`,
`main/Kconfig.projbuild`, `test/host/{test_log_ring.c,test_log_rotate.c,test_storage_log.c,CMakeLists.txt}`,
`test/emu/storage/v02-log-file.toml`, ARCHITECTURE §6.2a (stark_log), §8, TESTING §4.

**Deps.** STARK-0203.

**Notes.**
* **Capture** (`stark_log`): `stark_log_init()` installs an `esp_log_set_vprintf()` hook
  that forwards to the previous function (UART unchanged) and appends the formatted line
  (≤ 159 chars, truncated with `…`) to a static byte ring of `CONFIG_STARK_LOG_RING_BYTES`
  (4096; 0 disables capture) under a spinlock — no allocation, no blocking, whole lines
  only, oldest lines dropped when full and counted. API:
  `size_t stark_log_ring_take(char *buf, size_t max)` (whole lines, oldest first) and
  `uint32_t stark_log_ring_dropped(void)` (since last take). Lines logged before the card
  mounts stay in the ring, so the boot log reaches the file.
* **Writer** (`storage_log.c`, in the storage task): while mounted, every
  `CONFIG_STARK_STORAGE_LOG_FLUSH_MS` (1000) or when the ring is half full, appends the
  taken bytes to `logs/stark.log` via the STARK-0203 helpers (append mode, open and close
  per flush so a removal loses at most one flush), preceded by
  `--- <n> log lines dropped ---` when lines were lost. First write after each mount
  logs `storage: logging to logs/stark.log` once. The writer never logs on success
  (no feedback loop).
* **Rotation** (pure plan, `log_rotate.c`): before an append that would push `stark.log`
  past `CONFIG_STARK_STORAGE_LOG_FILE_KB` (64), remove `stark.<N-1>.log`, rename
  `stark.<i>.log` → `stark.<i+1>.log` downwards, rename `stark.log` → `stark.1.log`, with
  `N = CONFIG_STARK_STORAGE_LOG_FILES` (4): the log directory never exceeds
  `N × 64 kB` plus one flush. A failing step aborts that rotation and the append goes to
  the existing file; the next flush retries.
* Eject (STARK-0202) and removal: eject flushes first; removal keeps untaken lines in the
  ring. `storage: ejected log=<bytes written this mount>` replaces `storage: ejected`.
* `CONFIG_STARK_STORAGE_LOG_FILE` (y) turns the writer off without touching capture.
  **As built:** dropped — `STARK_LOG_RING_BYTES = 0` turns capture and therefore the file
  off, one knob instead of two. The writer reaches the file system through the public
  helpers plus two private engine calls (`storage_files_rename`, `storage_files_size`),
  runs outside the storage lock, and takes its ring through an injected function so the
  host tests feed it directly. Stack headroom is measured with a GDB dump of the stacks'
  fill pattern on the storage image (the Diagnostics app logs no task table):
  esp_timer task 2,500 of 3,584 B untouched, storage task 2,024 of 4,096 B.

**Host tests.** `test_log_ring.c`: whole-line semantics, wrap, oversize line, drop
counting, take into a small buffer; 100 % lines. `test_log_rotate.c`: plans for every
existing-file combination and N = 1…4; 100 % lines. `test_storage_log.c` on the POSIX ops
(temp dir) and `fault_io`: append, rotation sequence and bound after 1 MB of lines, a
failure at each rotation step, removal between take and write, dropped-lines marker.

**Emulator** (storage image). `v02-log-file.toml`: mounted → `storage: logging to
logs/stark.log`; navigate for a few seconds; Storage → `Eject` → OK →
`storage: ejected log=` with a non-zero count (`expect_re`); no panic; task table shows
every task's stack HWM ≥ 512 B (Diagnostics log line, `expect_re`).

**Fault injection.** Host: as above. Emulator: poke `present = 0` during a flush (no
panic, `storage: card removed`, the ring keeps capturing).

**AC.**
1. `v02-log-file.toml` passes in CI; no call site changed (`git diff` shows no
   `STARK_LOG*` edits outside `stark_log`).
2. The log directory is bounded by `N × size` + one flush (host, 1 MB run).
3. Logging never blocks the caller (spinlock + memcpy only; reviewed, and the 5 ms
   input timer shows no `drops=` in any scenario).
4. Host coverage: `log_ring.c` and `log_rotate.c` 100 %.

**Out of scope.** Log levels per sink, log export over USB (V0.7), timestamps beyond
esp_log's, compression, a log viewer app.

**Resource limits.** Ring 4 kB static; writer adds ≤ 512 B to the storage task's stack
HWM; per-line hook stack ≤ 192 B; flash ≤ +4 kB.

**CI.** Host tests, one scenario. **Security.** Logs stay on the device's own card;
STARK logs contain no secrets (AGENTS.md); later radio milestones decide what they may
log (SECURITY_SCOPE §5: transmissions are logged). **Physical HIL (deferred to V1).**
Write endurance and timing on a real card; log integrity after a real pull.

---

## STARK-0207 — V0.2 exit scenarios, soak, measurements, milestone close-out

**Goal.** Every V0.2 exit criterion's software half is demonstrated in CI and recorded,
and every physical half is listed, unclaimed, for V1.

**Layer.** Tests, CI, docs.

**Files.** `test/emu/v02-no-card.toml`, `test/emu/storage/{v02-write-under-render.toml,v02-remove-mid-write.toml,v02-unusable-card.toml}`,
`test/emu/soak/v02-soak.toml` (storage image), `.github/workflows/ci.yml` (soak job runs
both soaks), `docs/measurements.md`, `docs/VALIDATION.md` §3.5, `README.md`, TESTING
§4/§5, ROADMAP (V0.2 status).

**Deps.** STARK-0206.

**Notes.**
* `v02-no-card.toml` (production image, exit 4): no card → `storage: no card`; visit
  every app (About, Diagnostics, Input/Display/Buzzer/App Test, Hello, Storage — shows
  `No card`, write test refused —, Settings — change and restore a value); `drops=0`,
  `heap_min`.
* `storage/v02-write-under-render.toml` (exit 2): write test running, a DOWN press during
  it moves the selection (`menu: sel=` between `wtest start` and `wtest ok`),
  `frames=` non-zero, CRC line present.
* `storage/v02-remove-mid-write.toml` (exit 3): poke `present = 0` after
  `wtest start` → `storageapp: wtest failed: I/O error at <offset>`, `storage: card
  removed`, `dialog: open "SD card removed"`, no panic; poke `present = 1` → `storage:
  mounted`, `storage: swept 1 tmp`; a fresh write test then passes.
* `storage/v02-unusable-card.toml` (exit 4, storage image): poke `formatted = 0` and
  `present = 0` → removed → `present = 1` → `storage: unusable card: Not supported`;
  every app still reachable.
* `soak/v02-soak.toml` (storage image, 60 s emulated): V0.1 soak path plus Settings
  changes, two write tests, one remove/insert, one eject/mount; `drops=0`, root
  `heap_delta_max = 1024`. Runs in `esp-emulator-soak` with the V0.1 soak.
* `docs/measurements.md` V0.2 rows: heap at boot and at the root (production and storage
  images), storage/settings/log-ring RAM, flash delta per component, write-test duration
  *(emulated, informational)*, soak heap delta, task HWMs. A **V0.2 exit-criteria
  evidence** table with two columns per criterion: software evidence (links to CI
  scenarios and host tests) and **physical evidence: deferred to V1** (ROADMAP V1 exit 8).
* README status → "V0.2 complete (software-verified; physical microSD at V1)".

**AC.**
1. Every V0.2 exit criterion's software half demonstrably met, evidence in
   `docs/measurements.md`; every physical half listed as deferred, none claimed.
2. All V0.2 scenarios and both soaks pass in CI; `check_emu_image.py` green.
3. `grep -rIiE 'wokwi|qemu|esp_emu|emu_medium' components/ apps/ main/` is empty.
4. No scenario asserts a wall-clock value.
5. ADR-0008's risk recorded as open in ARCHITECTURE §13 with the V1 measurement that
   closes it.

**As built.** The test medium re-registers the mounted volume's diskio with
wear-levelling calls gated on `emu_medium_present`, so a pull fails the next sector
read/write (ADR-0018 §3); `v02-remove-mid-write` pulls at 1 500 ms emulated, 20 480 B
into the stream (deterministic), and forbids a failure at offset 0. The V0.2 soak runs on
the storage image (it lives in `test/emu/soak/`; the harness unit test allows storage
scenarios there), and the `esp-emulator-soak` job builds that image too.

**Out of scope.** Any hardware work; V0.3 tasks.

**CI.** Scenarios in the existing jobs; the soak job runs the V0.2 soak too.
**Security.** None new. **Physical HIL (deferred to V1).** ROADMAP V1 exit 8 in full.

---

# V0.3 — infrared lab

**Scope source:** [ROADMAP.md](ROADMAP.md) "V0.3". Nothing from V0.4+ is pulled forward;
no protocol beyond NEC, NEC-extended, RC5, RC6 mode 0 and Sony SIRC (12/15/20-bit) plus
the raw format; no code databases, brute or sweep transmission (SECURITY_SCOPE §3).

**Completion levels (ADR-0019, owner decision 2026-10-24).** *V0.3 complete* means IR
**software / protocol verified**. It never means physical IR verified. Every task lists
its software evidence and its **physical HIL evidence (deferred to V1)**, collected in
ROADMAP V1 exit 9, the V1 hardware table and `docs/bringup/v1-ir.md`.

**Validation layers** (each task uses the narrowest one that proves its claim):

* **Host (Tier 1)** — the pure IR core: timing match, every codec, the raw format, symbol
  conversion, the capture ring and the transmit gate, against committed **spec-derived
  vectors** (`test/host/vectors/ir_*.h`, generated by `scripts/gen_ir_vectors.py` from
  published protocol timings with seeded jitter; ADR-0019 §3) and malformed input.
* **Host UI port (Tier 4B)** — the IR app's screens, inspected goldens, with a fake
  `stark_ir`.
* **Emulator, production image (Tier 3)** — boot, RMT channel creation, RX/TX lifecycle,
  and with the scenario key `rmt_loopback = "0:2"` the production firmware's own
  transmissions received back: encoder → RMT TX → (baseband loop) → RMT RX → symbol
  conversion → decoder → event → app/log. Carrier, light and receiver are **not** in
  this path (ADR-0019 §2).
* **Emulator, storage test image** — the same, plus SD: captures seeded at build time
  into its FAT partition (`test/emu/storage_image/seed/stark/captures/*.ir`, spec-derived,
  documented) are browsed and replayed, received back and saved.

**Hardware.** None. `IR_TX` = GPIO 47 and `IR_RX` = GPIO 48 are already reserved
(HARDWARE.md §2.2); STARK-0303 moves them into `stark_board`.

**Exit-criteria map** (ROADMAP V0.3; each row's V1 half is ROADMAP V1 exit 9):

| Exit criterion | V0.3 software evidence | Proven by |
| --- | --- | --- |
| 1. ≥ 20 vectors per protocol | ≥ 20 spec-derived host vectors per protocol + encode → loopback → decode on the production firmware for every protocol | STARK-0301, STARK-0302 (host), STARK-0304 (loopback self-test), STARK-0307 (`storage/v03-protocols.toml`) |
| 2. Capture, save, replay | capture → save → browse → replay → captured again through the loopback on the storage image; gated, confirmed, logged | STARK-0305, STARK-0306, STARK-0307 (`storage/v03-capture-replay.toml`) |
| 3. Raw capture/replay of an unsupported protocol | raw round trip on the host; a raw-only seeded signal replayed and captured back within tolerance | STARK-0302, STARK-0307 |

**System event id** (ARCHITECTURE §6.3): 4 `STARK_SYS_IR` (`arg` = `STARK_IR_EVT_RX` 1, a
capture is ready; `STARK_IR_EVT_TX_DONE` 2; `STARK_IR_EVT_TX_FAILED` 3).

**Launcher.** The IR app forms a new category **Lab**, appended after Device; it becomes
the last row (14), so UP-from-About wrap paths in scenarios gain one press (STARK-0305).

**Common definitions** (`components/stark_ir/include/ir_types.h`, pure, used by every task):

```c
#define IR_MAX_DURATIONS 256          /* marks and spaces of one capture/transmission */
typedef enum { IR_PROTO_UNKNOWN = 0, IR_PROTO_NEC, IR_PROTO_NECX, IR_PROTO_RC5,
               IR_PROTO_RC6, IR_PROTO_SIRC12, IR_PROTO_SIRC15, IR_PROTO_SIRC20,
               IR_PROTO_COUNT } ir_protocol_t;
typedef struct { uint16_t n; uint16_t us[IR_MAX_DURATIONS]; } ir_timings_t; /* mark first */
typedef struct {
    ir_protocol_t protocol;
    uint16_t address;   /* NEC 8, NECX 16, RC5 5, RC6 8, SIRC 5/8/13 bits */
    uint16_t command;   /* NEC/NECX 8, RC5 7 (incl. field bit), RC6 8, SIRC 7 */
    bool repeat;        /* NEC/NECX repeat code */
    uint8_t toggle;     /* RC5/RC6 toggle bit */
} ir_frame_t;
typedef struct { ir_timings_t timings; uint32_t carrier_hz; ir_frame_t frame; } ir_signal_t;
```

---

## STARK-0301 — IR codec core: timing model, NEC/NEC-extended, vector generator

**Goal.** The protocol-independent timing model and the NEC family encode and decode
exactly, tolerate realistic timing error and reject malformed frames, proven on ≥ 20
deterministic vectors per protocol.

**Layer.** L2 pure core inside the new component `stark_ir` (no ESP-IDF; the RMT port
arrives with STARK-0303). Host test infrastructure.

**Files.** `components/stark_ir/{include/ir_types.h,include/ir_codec.h,ir_timing.{c,h},ir_nec.c,ir_codec.c,CMakeLists.txt}`
(new; target build compiles the pure sources), `scripts/gen_ir_vectors.py` (new) +
`scripts/test_gen_ir_vectors.py`, `test/host/vectors/ir_nec_vectors.h` (generated,
committed), `test/host/{test_ir_nec.c,test_ir_timing.c,CMakeLists.txt}`,
`scripts/test_host.sh` (coverage filter), `scripts/check_layers.py` (`stark_ir` L3),
ARCHITECTURE §6.9 (deferred row → contract stub), §10.

**Deps.** STARK-0207.

**Notes.**
* **Timing match** (`ir_timing.c`): `bool ir_match(uint32_t measured_us, uint32_t
  nominal_us, uint8_t tol_pct)` — inside `nominal ± max(tol_pct %, 60 µs)` (the absolute
  floor covers short marks where a percentage is smaller than receiver quantisation).
  Default tolerance `IR_TOL_PCT` 25. Boundary behaviour is part of the contract and
  tested at nominal ± (tol − 1) % and ± (tol + 1) %.
* **Codec API** (`ir_codec.h`):
  ```c
  stark_err_t ir_encode(const ir_frame_t *f, ir_timings_t *out, uint32_t *carrier_hz);
  ir_protocol_t ir_decode(const ir_timings_t *t, ir_frame_t *out);   /* UNKNOWN: no match */
  const char *ir_protocol_name(ir_protocol_t p);                     /* "NEC", "RC6" … */
  ```
  `ir_decode` tries each protocol decoder in a fixed order and returns the first that
  matches completely; protocol-specific decoders never see raw-format concerns.
* **NEC** (38 kHz): leader 9000/4500, bit mark 560, space 560 (0) / 1690 (1), LSB first,
  32 bits = address, ~address, command, ~command, stop mark 560. A frame whose address
  byte and its inverse do not complement is **NEC-extended** (16-bit address) when the
  command pair complements; a command pair that does not complement is rejected
  (malformed). Repeat code 9000/2250/560 → `repeat = true`, address/command 0.
  Encoding NEC requires address ≤ 0xFF; NECX encodes the 16-bit address verbatim and is
  decoded as NECX only when its low/high bytes are not complements (otherwise it is, by
  definition, standard NEC — documented, and a vector covers it).
* **Vectors** (`gen_ir_vectors.py`, standard library only, seeded PRNG, output byte-stable
  and checked in CI by regenerating and diffing): per protocol ≥ 20 frames — nominal
  timings; uniform jitter up to ± 15 %; worst-case ± 24 % on every duration (must decode);
  ± 26 % on one duration (must not decode as that protocol); edge values (address/command
  0x00, 0xFF, 0x5A/0xA5 patterns); repeat codes; truncated frames; an extra pulse; a
  missing stop mark. Each vector records `{label, timings[], expect_protocol, address,
  command, repeat, toggle}`. Header comment: "spec-derived, not recorded (ADR-0019 §3)".

**Host tests.** `test_ir_timing.c`: tolerance boundaries, floor, zero/overflow inputs.
`test_ir_nec.c`: every NEC/NECX vector decodes as labelled; encode(frame) equals the
nominal timing list exactly; encode → decode round trip for all 65 536 NEC
address/command combinations (cheap) and a 4 096-sample sweep of NECX; malformed classes
rejected (`IR_PROTO_UNKNOWN`). `ir_timing.c`, `ir_nec.c`, `ir_codec.c` 100 % lines.

**Emulator.** None (no driver yet); the target build compiles `stark_ir`'s pure sources.

**Fault injection.** The malformed vector classes above; `ir_decode` on `n = 0`,
`n = IR_MAX_DURATIONS`, odd/even `n`, durations of 0 and 65 535.

**AC.**
1. ≥ 20 NEC and ≥ 20 NECX vectors, generated deterministically, committed, regenerated
   identically in CI; all pass.
2. Tolerance boundaries behave as specified (± 24 % decodes, ± 26 % does not) on every
   duration class, not tuned to one frame.
3. Round trips exhaustive (NEC) and sampled (NECX); malformed frames rejected.
4. 100 % lines of the three core files; `check_layers.py` classifies `stark_ir`.

**Out of scope.** RC5/RC6/SIRC and the raw format (STARK-0302), any RMT code
(STARK-0303/0304), NEC variants beyond NEC/NECX (Samsung32, Apple…).

**Resource limits.** Core code ≤ 6 kB flash; no allocation; `ir_timings_t` 514 B (callers
decide where it lives).

**CI.** New host tests; `lint` regenerates the vectors and fails on a diff.
**Security.** Encode-only of user-chosen frames; no code tables. **Physical HIL
(deferred to V1).** ≥ 20 NEC/NECX vectors recorded from our own remotes and decoded.

---

## STARK-0302 — IR codec core: RC5, RC6 mode 0, Sony SIRC, raw timing format

**Goal.** The remaining protocols encode and decode under the same rules, and any
capture — decoded or not — round-trips through a documented text format for SD.

**Layer.** L2 pure core in `stark_ir`.

**Files.** `components/stark_ir/{ir_rc5.c,ir_rc6.c,ir_sirc.c,ir_raw.{c,h},ir_codec.c,include/ir_codec.h,CMakeLists.txt}`,
`scripts/gen_ir_vectors.py`, `test/host/vectors/ir_{rc5,rc6,sirc}_vectors.h`,
`test/host/{test_ir_codecs.c,test_ir_raw.c,CMakeLists.txt}`,
`scripts/test_host.sh`, `docs/ir-format.md` (new: the `.ir` file format).

**Deps.** STARK-0301.

**Notes.**
* **RC5** (36 kHz): bi-phase (Manchester), unit 889 µs, 14 bits MSB first: S1 = 1, S2 =
  inverted bit 6 of the command (RC5 with the extended field), toggle, 5 address bits,
  6 command bits; a 1 is space-then-mark. Decoding works from half-bit edges; a capture
  starting with the first mark (the leading space is invisible) is the normal case.
* **RC6 mode 0** (36 kHz): leader mark 2666 / space 889, start bit 1, mode bits 000,
  trailer (toggle) bit of double length, 8 address bits, 8 command bits, unit 444 µs,
  bi-phase with RC6's polarity (1 = mark-then-space). Other RC6 modes decode as
  `IR_PROTO_UNKNOWN` (raw).
* **SIRC** (40 kHz): start mark 2400, space 600, bit marks 1200 (1) / 600 (0), LSB first:
  7 command bits then 5 (12-bit), 8 (15-bit) or 5 + 8 (20-bit: address 5 bits, extended 8
  bits → `address` holds 13 bits) address bits; the variant is the bit count.
* **Raw format** (`ir_raw.c`, `docs/ir-format.md`): a small line-based text file, so a
  user can read it on a PC:
  ```
  stark-ir 1
  protocol NEC            # or RAW for an undecoded capture
  address 0x00F7
  command 0x0012
  carrier 38000
  timings 9000 4500 560 560 … 560
  ```
  `ir_raw_format(const ir_signal_t *, char *out, size_t len)` and
  `stark_err_t ir_raw_parse(const char *text, size_t len, ir_signal_t *out)`. The parser
  is strict and bounded: unknown keys ignored, missing `timings` or `stark-ir 1` →
  `INVALID_ARG`, more than `IR_MAX_DURATIONS` values, a value of 0 or > 65 535, a carrier
  outside 20–60 kHz, non-numeric tokens or an over-long line → `INVALID_ARG`; never reads
  past `len`. `timings` are authoritative on replay; the decoded fields are informational
  and re-derived (`ir_decode`) after parsing — a file whose fields disagree with its
  timings is accepted with the decoded values (documented).

**Host tests.** One file per protocol: every vector (≥ 20 each, same classes as
STARK-0301 adapted to bi-phase: half-bit jitter, a missing half-bit, a doubled edge),
exact nominal encodings, exhaustive round trips (RC5 all 2 × 32 × 128, RC6 all 2 × 256 ×
256, SIRC all variants × all values). `test_ir_raw.c`: format → parse round trip for
every vector (decoded and RAW); a 256-duration capture; every malformed-input class
above; a truncated buffer; fuzz loop of 10 000 seeded random byte strings never crashing
(ASan/UBSan). 100 % lines of each new file.

**Emulator.** None.

**Fault injection.** Malformed bi-phase streams, decoder ambiguity (a capture valid for
two protocols — the documented order decides, tested), parser fuzzing.

**AC.**
1. ≥ 20 vectors each for RC5, RC6-0, SIRC-12, SIRC-15, SIRC-20; all pass; tolerance
   boundaries as in STARK-0301.
2. Exhaustive round trips pass; malformed streams rejected.
3. `docs/ir-format.md` describes the format; parser fuzzing clean under ASan/UBSan.
4. 100 % lines of the new core files.

**As built.** One host file covers the five bi-phase/pulse-width tables
(`test_ir_codecs.c`) instead of one per protocol. Bi-phase decoding classifies each
duration as k units (k = round(d / unit), then a ± 25 % check against k units); RC6 tries
the nominal unit first and then a unit estimated from its leader, used only when the
leader's mark and space agree within 10 % — uniform ± 24 % scaling decodes, one pulse off
by 26 % does not. The generator's "doubled edge" class was replaced by "over-long pulse":
splitting a pulse only shifts the bi-phase phase and can produce another valid frame
(RC5/RC6 carry no checksum), so it is not a malformed-input test.

**Out of scope.** RC6 modes ≠ 0, RC-MM, Samsung, Panasonic/Kaseikyo, JVC, learning
heuristics, compression of raw timings.

**Resource limits.** All codecs + raw ≤ 14 kB flash; parse/format allocate nothing.

**CI.** Host tests; vectors regenerated in `lint`. **Security.** The parser is the
first code that reads user files from SD: bounded, strict, fuzzed. **Physical HIL
(deferred to V1).** ≥ 20 recorded vectors per protocol from our own remotes.

---
## STARK-0303 — `stark_ir` RMT RX driver

**Goal.** IR frames arriving on `IR_RX` are captured by the ESP-IDF v6 RMT RX driver,
converted to timings, decoded and offered to the UI as events — without allocation,
without blocking the ISR, and with every failure counted.

**Layer.** L3 port of `stark_ir` (`stark_ir.c`) + L2 pure helpers (`ir_symbols.c`,
`ir_capture.c`); L0 `stark_board` (pins).

**Files.** `components/stark_board/{include/stark_board.h,board_devkitc1.c,stark_board.c}`
(`ir_tx` 47, `ir_rx` 48), `diagram.json` / `scripts/check_pins.py` as the pin check
requires, `components/stark_ir/{include/stark_ir.h,stark_ir.c,ir_symbols.{c,h},ir_capture.{c,h},Kconfig.stark,CMakeLists.txt}`,
`main/{stark_main.c,Kconfig.projbuild,CMakeLists.txt}`, `scripts/{check_layers.py,test_emu.py,test_emu_harness.py}`
(`rmt_loopback` key), `test/host/{test_ir_symbols.c,test_ir_capture.c,CMakeLists.txt}`,
`test/emu/v03-ir-rx.toml`, HARDWARE.md §2, ARCHITECTURE §6.1/§6.3/§6.9/§7/§8/§9/§10,
docs/VALIDATION.md §2.

**Deps.** STARK-0302.

**Notes.**
* **API** (`stark_ir.h`):
  ```c
  #define STARK_SYS_IR 4u
  enum { STARK_IR_EVT_RX = 1, STARK_IR_EVT_TX_DONE = 2, STARK_IR_EVT_TX_FAILED = 3 };
  stark_err_t stark_ir_init(void);          /* channels created, both idle */
  stark_err_t stark_ir_rx_start(void);      /* receive-first: the app turns RX on */
  stark_err_t stark_ir_rx_stop(void);
  stark_err_t stark_ir_take(ir_signal_t *out);  /* oldest capture; NOT_FOUND if none */
  typedef struct { uint32_t frames, decoded, unknown, overflows, dropped, tx; } stark_ir_stats_t;
  stark_err_t stark_ir_stats(stark_ir_stats_t *out);
  ```
  Board without IR pins (`-1`): `init` logs `ir: not fitted`, everything else returns
  `STARK_ERR_NOT_SUPPORTED`.
* **RX channel:** `rmt_new_rx_channel` on `IR_RX`, 1 MHz resolution (1 tick = 1 µs),
  `mem_block_symbols` large enough for `IR_MAX_DURATIONS / 2` symbols without DMA (the
  ESP32-S3 lets one RX channel take consecutive blocks; record the value chosen),
  `rmt_receive` with `signal_range_min_ns` 1250 (glitch filter) and
  `signal_range_max_ns` 12 000 000 (12 ms of idle ends a frame: longer than any space
  inside a supported frame — NEC's 4.5 ms — and shorter than any inter-frame gap). The
  done callback (ISR) only copies the symbol count and re-arms into the other half of a
  static double buffer, then notifies the IR task; a frame that fills the buffer is
  flagged and counted as an overflow.
* **IR task** (static, prio `CONFIG_STARK_IR_TASK_PRIO` 3, stack
  `CONFIG_STARK_IR_TASK_STACK` 3072, core 0): converts symbols to timings
  (`ir_symbols_to_timings`, pure: RMT `{level0,duration0,level1,duration1}` pairs → an
  alternating mark/space list, merging same-level neighbours, dropping a trailing zero
  duration), runs `ir_decode`, pushes into the capture ring (`ir_capture.c`, pure, 4
  slots, drop-oldest, counted), publishes `STARK_SYS_IR`/`RX`, and logs one line per
  frame (contract): `ir: rx NEC addr=0x00 cmd=0x12` / `ir: rx NEC repeat` /
  `ir: rx RAW n=<durations>`.
* `stark_ir_init()` in `app_main` after `stark_storage_init()`; RX stays off until an
  app starts it (receive-first, SECURITY_SCOPE §4.1).
* **Harness:** scenario key `rmt_loopback = "0:2"` passes `--rmt-loopback 0:2` (the
  verified pair for the first TX and first RX channel on the esp32s3 model, ADR-0019 §2).
* `check_layers.py`: `stark_ir` L3, sideways `stark_ir → stark_event`.

**Host tests.** `test_ir_symbols.c`: symbol pairs → timings for every vector's nominal
encoding (via a pure timings → symbols helper that STARK-0304 reuses), merging, trailing
zeros, a single-level frame, an empty buffer, the maximum length and one more.
`test_ir_capture.c`: ring order, drop-oldest counting, take on empty. 100 % lines.

**Emulator.** `v03-ir-rx.toml` (production image): boot → `ir: init rx=48 tx=47`;
the IR channels exist without disturbing anything else (all V0–V0.2 scenarios still
pass); RX start/stop is exercised by STARK-0304's self-test. No injected frames (the
emulator offers no pad stimulus; frames arrive through the loopback from STARK-0304 on).

**Fault injection.** Host: overflowing captures, symbol streams with zero durations,
ring overflow. Emulator: none beyond boot until TX exists.

**AC.**
1. The production image boots with the RMT RX channel created (`ir: init …`); every
   earlier scenario passes unchanged; heap ≥ 200 kB.
2. `ir_symbols.c`, `ir_capture.c` 100 % lines.
3. Pins only in `stark_board`; `check_pins.py`, `check_layers.py` pass;
   `grep -rIiE 'wokwi|qemu|esp_emu|loopback' components/ apps/ main/` empty.
4. ARCHITECTURE documents `stark_ir` RX, the event id and the task.

**As built.** The RX hardware memory is 64 symbols (the driver ping-pongs it into a
130-symbol buffer; no DMA). Boot heap dropped 9 568 B, not the ≤ 4 kB estimated above:
≈ 6.3 kB is static (capture ring 2.1 kB, IR task stack 3 kB, symbol buffer and work
signal ≈ 1 kB) and ≈ 3.2 kB the RMT driver's own allocation; free heap stays at 279 kB
(gate 200 kB). The port is target-only (as `stark_storage.c`); the pure parts are 100 %.

**Out of scope.** TX and carrier (STARK-0304), the app (STARK-0305), learning across
multiple frames, protocol auto-repeat merging beyond NEC's repeat code.

**Resource limits.** Static RAM ≤ 6 kB (double symbol buffer + ring of 4 × 514 B +
task stack 3 kB); heap at boot drops ≤ 4 kB; flash ≤ +40 kB (RMT driver).

**CI.** New task, host tests, one scenario, harness key. **Security.** Receive-only, off
by default. **Physical HIL (deferred to V1).** TSOP-class receiver output on GPIO 48 into
RMT RX on silicon; glitch filter and idle threshold against real remotes; the
DevKitC-1 v1.0 RGB LED on GPIO 48 (cosmetic, HARDWARE.md §2.2) checked.

---

## STARK-0304 — `stark_ir` RMT TX driver with carrier and safety gating

**Goal.** A signal is transmitted on `IR_TX` with its protocol's carrier, only through a
gate that enforces SECURITY_SCOPE §4's transmit rules, every transmission is logged, and
the production firmware's own transmission is received back through the emulator
loopback for every protocol.

**Layer.** L3 port of `stark_ir` + L2 pure gate (`ir_tx_guard.c`); App Test (L5) gains a
self-test item.

**Files.** `components/stark_ir/{stark_ir.c,ir_tx_guard.{c,h},ir_symbols.{c,h},include/stark_ir.h,Kconfig.stark}`,
`apps/app_apptest/**` (`IR self-test`), `test/host/{test_ir_tx_guard.c,test_ir_symbols.c,CMakeLists.txt}`,
`test/emu/v03-ir-loopback.toml`, ARCHITECTURE §6.9, TESTING §4, docs/VALIDATION.md §2/§3.6.

**Deps.** STARK-0303.

**Notes.**
* **API:**
  ```c
  /* Starts a transmission and returns at once; STARK_SYS_IR TX_DONE / TX_FAILED follows.
     Call only after the user confirmed (SECURITY_SCOPE §4.2). */
  stark_err_t stark_ir_transmit(const ir_signal_t *s, uint8_t repeats);
  ```
  Errors: `NOT_SUPPORTED` (no IR on this board), `INVALID_ARG` (rejected by the gate),
  `BUSY` (a transmission is running or the minimum interval has not elapsed).
* **Gate** (`ir_tx_guard.c`, pure, host-tested): durations 1 … `IR_MAX_DURATIONS`, each
  50 … 65 535 µs, an odd count (ends on a mark); total signal time ≤ 1 000 ms; carrier
  30–60 kHz (default by protocol: NEC/NECX 38 kHz, RC5/RC6 36 kHz, SIRC 40 kHz; RAW
  keeps its captured value, else 38 kHz); duty fixed at 33 %; repeats ≤ 3 per call with
  the protocol's frame period (NEC 108 ms, RC5/RC6 114 ms, SIRC 45 ms, RAW: 100 ms gap);
  at least `CONFIG_STARK_IR_TX_MIN_INTERVAL_MS` (1000) between the end of one
  transmission and the start of the next. No "repeat forever" exists in the API.
* **TX channel:** `rmt_new_tx_channel` on `IR_TX`, 1 MHz, `rmt_apply_carrier`
  (frequency from the signal, duty 0.33), a copy encoder over the symbols produced by
  `ir_timings_to_symbols()` (pure). The IR task waits for `rmt_tx_wait_all_done` with a
  timeout of total time + 100 ms; a timeout disables and re-enables the channel and
  reports `TX_FAILED`.
* **Log (guardrail 5), one line per transmission:**
  `ir: tx NEC addr=0x00 cmd=0x12 carrier=38000 dur=<ms> repeats=<k>` (RAW:
  `ir: tx RAW n=<durations> carrier=<hz> dur=<ms> repeats=<k>`), then `ir: tx done` or
  `ir: tx failed: <err>`. The log file (STARK-0206) keeps them.
* **App Test → `IR self-test`:** confirm dialog ("Transmit IR test frames?", naming the
  protocols and carriers) → RX on → transmits one fixed frame per protocol (NEC 0x00/0x12,
  NECX 0x1234/0x56, RC5 0x05/0x35, RC6 0x10/0x20, SIRC-12/15/20 fixed values — the
  project's own lab test values) one after another, honouring the gate's interval →
  compares each received frame with what was sent → `apptest: ir self-test <proto>
  ok|mismatch|timeout` and a final summary. On real hardware it needs the LED and the
  receiver to see each other; in the emulator it runs through the loopback.

**Host tests.** `test_ir_tx_guard.c`: every rule and its boundary (1 000 ms exactly / + 1,
3 vs 4 repeats, 29 999 / 30 000 Hz, the interval at − 1 ms / exactly), carrier defaults
per protocol. `test_ir_symbols.c`: timings → symbols → timings identity for every vector.
100 % lines of `ir_tx_guard.c`.

**Emulator.** `v03-ir-loopback.toml` (production image, `rmt_loopback = "0:2"`): App
Test → IR self-test → confirm → for every protocol `ir: tx <proto> …`, `ir: rx <proto>
addr=… cmd=…` with the same values, `apptest: ir self-test <proto> ok`; then the gate:
an immediate second run is refused (`BUSY`) until the interval elapses; repeated
self-tests (×3) leave the heap unchanged (`heap_delta_max`). What this proves and does
not prove is stated in the scenario header (ADR-0019 §2).

**Fault injection.** Host: every gate rejection. Emulator: a transmission with the
loopback **off** (scenario without `rmt_loopback`) → every protocol reports
`timeout`, no panic, RX/TX still usable afterwards.

**AC.**
1. `v03-ir-loopback.toml` passes in CI: all seven protocol variants encoded → RMT TX →
   loopback → RMT RX → decoded to the sent values on the production firmware.
2. The no-loopback scenario reports timeouts without a panic.
3. Every transmission is logged with protocol, payload, carrier, duration and repeats;
   none happens without a confirmation (dialog line precedes every `ir: tx`).
4. `ir_tx_guard.c` 100 % lines; gate boundaries tested.
5. Heap unchanged across three self-test runs (± 256 B).

**As built.** App Test's self-test is item 7 (`IR self-test`, reached with UP from the
first item). Its worker retries a `BUSY` from the interval rule every 50 ms, so the
"immediate second run is refused" check lives in the host gate tests rather than the
scenario. The three-run heap check is a throwaway run (heap 276 084 → 276 084 B, Δ 0),
like V0.2's remove/insert check. The host UI port gained a fake `stark_ir`
(`test/host/port/ui_port_ir.c`), and the host build caught a missing `<stddef.h>` in
`ir_tx_guard.c` that the firmware build had hidden.

**Out of scope.** Transmit power control, carrier sweeps, TX from anywhere but a
confirmed user action, sequences of different signals in one transmission.

**Resource limits.** TX symbol buffer ≤ 1 kB static; TX adds ≤ 1 kB heap; flash ≤ +8 kB.

**CI.** Host tests, two scenarios (loopback and no-loopback). **Security.** The gate is
the enforcement point for SECURITY_SCOPE §4 (receive-first, confirmation, no loops, rate,
logging); it is not user-adjustable. **Physical HIL (deferred to V1).** LED current
through the NPN driver; carrier frequency/duty and frame timing on a logic analyser for
every protocol; the self-test with a real LED facing a real receiver; own-echo while
transmitting.

---

## STARK-0305 — IR app: live receive view + save to SD

**Goal.** The IR app receives continuously while open, shows each capture (protocol,
address, command, raw timings) and saves the selected one to the card.

**Layer.** L5 app; composition root (registry).

**Files.** `apps/app_ir/{app_ir.c,ir_view.{c,h},ir_files.{c,h},CMakeLists.txt}` (new),
`main/app_registry.{c,h}`, `components/stark_ir/ir_names.{c,h}` (pure: next free
`ir_NNNN.ir` name), `test/host/{test_ui_ir.c,test_ir_names.c,port/ui_port_ir.{c,h},CMakeLists.txt}`,
`test/emu/storage/v03-ir-app.toml`, existing scenarios' key paths and registry lines,
ARCHITECTURE §6.7 (launcher categories), TESTING §4.

**Deps.** STARK-0304.

**Notes.**
* App id `ir`, title `IR`, category `Lab` (new, last). On start RX goes on; on stop it
  goes off (receive-first; nothing keeps receiving after the app).
* **Receive view:** the last 6 captures, newest first: `NEC 00/12`, `RC5 05/35 t1`,
  `RAW 67`, with a detail row for the selected one (carrier, duration count, the first
  timings); `STARK_SYS_IR`/`RX` drains `stark_ir_take()`. Items `Save` and `Browse`
  (STARK-0306). Logs `ir: view <proto> …` per new capture shown.
* **Save:** writes the selected capture with `ir_raw_format` to
  `captures/ir_NNNN.ir` (next free number, `ir_names.c`) via `WRITE_ATOMIC` on the app
  worker; `irapp: saved ir_0001.ir` / `irapp: save failed: <err>`; no card →
  `irapp: save refused: no card`.
* Launcher: Lab header row 13, IR row 14. Scenarios that wrap UP from About to Storage
  gain one UP; the V0.1 soak and `v01-hello` follow (as at STARK-0202/0205).

**Host tests.** `test_ir_names.c`: next name from a directory listing (gaps, 9999,
non-matching names). `test_ui_ir.c` on the host UI port with a fake `stark_ir`: captures
of each protocol render as expected (goldens inspected), newest first, detail row, save
refused without a card. 100 % lines of `ir_names.c`.

**Emulator.** `v03-ir-app.toml` (storage image): opening the IR app turns RX on
(`ir: rx on`) and leaving it turns RX off (`ir: rx off`); the empty view logs nothing;
`Save` with nothing captured is refused (`irapp: save refused: nothing captured`); the
launcher reaches IR on row 14. Capturing and saving a real frame needs a transmitter —
in the emulator that is the app's own replay through the loopback, covered by STARK-0306.

**Fault injection.** Host: save with the card full (`NO_SPACE`), removed mid-save
(`IO`), name space exhausted.

**AC.**
1. RX is on exactly while the IR app runs (scenario).
2. Goldens inspected; captures of every protocol rendered; save errors surfaced.
3. `ir_names.c` 100 %; app files ≤ 150 lines each.
4. Earlier scenarios pass with updated key paths only.

**As built.** The IR app is registered before Storage and Settings, so the **Lab**
category sits ahead of Device: rows Lab header 10, IR 11, Device header 12, Settings 13,
Storage 14. Storage stays the last row, so the storage scenarios' UP-wrap paths are
unchanged; only `sel=` numbers, `v01-hello` (four UPs), the V0.1 soak (four DOWNs after
Hello) and `v02-no-card` (now visits IR too) move. Rows: `Save last`, `Browse saved`,
then six fixed capture rows (blank and disabled until filled — the menu model counts
rows once); OK on a capture row saves that capture. The save runs on the app worker.

* **Static RAM is 5 984 B, not ≤ 4 kB** (measured at STARK-0307 with `nm`). The
  limit above assumed compact summaries plus one full signal, but the goal and notes
  have the *selected* capture saved, so all six keep their full timings (`s_caps`,
  3 192 B); the `.ir` text and the signal handed to the worker add 2 332 B (shared with
  Browse from STARK-0306); labels, values, rows and the menu 460 B. Boot heap stays far
  above the 200 kB gate.

**Out of scope.** Editing captures, renaming files, live waveform plots, protocol
"learning" across frames.

**Resource limits.** App static RAM ≤ 4 kB (6 compact capture summaries + one full
`ir_signal_t` for save); saves run on the worker.

**CI.** Host tests, scenario. **Security.** Receive-only screen; files go only to
`captures/` through the validated helpers. **Physical HIL (deferred to V1).** Capturing
our own remotes through a real receiver.

---

## STARK-0306 — IR app: browse and replay saved signals (own devices only)

**Goal.** Saved signals are listed, inspected and transmitted after a confirmation that
names what will be sent; the replay is received back through the loopback and can be
saved again.

**Layer.** L5 app.

**Files.** `apps/app_ir/{ir_browse.{c,h},app_ir.c,ir_files.c}`, `test/host/{test_ui_ir.c}`,
`test/emu/storage_image/seed/stark/captures/*.ir` (new, spec-derived, documented),
`test/emu/storage_image/CMakeLists.txt` (seed the FAT partition at build time with
ESP-IDF's FAT image generator), `scripts/check_emu_image.py` (allows the seed),
`test/emu/storage/v03-replay.toml`, docs/VALIDATION.md §3.6.

**Deps.** STARK-0305.

**Notes.**
* **Browse:** lists `captures/*.ir` (newest number first, at most 64 shown), each parsed
  with `ir_raw_parse` on the worker; unreadable files show `(invalid)` and cannot be
  sent. Detail screen: protocol, address, command, carrier, duration count, total time.
* **Replay:** `Send` opens `stark_ui_dialog_confirm("Transmit IR?", "<proto> <addr>/<cmd>
  <carrier> Hz, <ms> ms — own devices only")`; OK → `stark_ir_transmit(signal, 0)`;
  `irapp: sent <file>` on `TX_DONE`, `irapp: send failed: <err>` otherwise; a `BUSY`
  from the interval rule shows "Wait a moment". No repeat key, no auto-send, no list
  send.
* **Seeds (storage test image only):** one file per protocol variant plus a RAW-only
  signal that matches no protocol (`ir_9001.ir` … `ir_9008.ir`), generated by
  `gen_ir_vectors.py`, header comment "spec-derived lab vectors, not recorded".
* Because the IR app keeps RX on, a replay is received back through the loopback and
  appears in the receive view — so `Save` stores it as a new file.

**Host tests.** `test_ui_ir.c`: browse list and detail goldens, invalid file row, the
confirm dialog text names protocol/payload/carrier/duration, cancel sends nothing, BUSY
message.

**Emulator.** `storage/v03-replay.toml` (storage image, `rmt_loopback`): IR → Browse →
`ir_9001.ir` (NEC) → Send → `dialog: open "Transmit IR?"` → OK → `ir: tx NEC …` →
`ir: rx NEC addr=… cmd=…` (same values) → `irapp: sent ir_9001.ir` → back → Save →
`irapp: saved ir_0001.ir` → Browse shows it first → Send it → received again with the
same values. Then the RAW seed: sent and `ir: rx RAW n=<same count>`.

**Fault injection.** A corrupted seed (`ir_9099.ir`, malformed) shows `(invalid)` and
cannot be sent; card removed while browsing → list empties, no panic; a second Send
inside the interval → "Wait a moment".

**AC.**
1. `v03-replay.toml` passes: capture → save → browse → replay → captured again through
   the loopback, and RAW replay captured back (ROADMAP V0.3 exits 2 and 3, software half).
2. No transmission without the confirmation dialog (log order checked).
3. Invalid files cannot be sent; goldens inspected.
4. `check_emu_image.py` still proves the storage image differs only in the medium (and
   now its seeded partition content).

**As built.**
* **Files.** The app keeps every file at 150 lines or fewer (the app-size rule), and is
  split as follows. `app_ir.c` holds the screen, mode switch and lifecycle; `ir_view.c`
  the receive view (STARK-0305's capture list, moved out); `ir_browse.c` the listing and
  loading; `ir_send.c` the confirmation and transmission; `ir_save.c` card I/O (save and
  load). All declarations are in the app's private `irapp.h`, with no per-file headers.
  The browse order ("saved captures first, newest first, then the rest by name") is pure,
  so it moved into `stark_ir`'s `ir_names_compare()` with host tests. `ir_tx_guard.h` became a public
  header of `stark_ir` so the confirm dialog shows the plan the driver will enforce;
  `stark_ir_transmit()` re-checks regardless.
* **Seeds** are `lab_nec.ir … lab_sirc20.ir`, `lab_raw.ir` (13 durations) and the
  malformed `lab_bad.ir`, not `ir_9001…ir_9099`: `ir_NNNN` names feed the save counter, so
  seeds named that way would make the first user save `ir_9100.ir`. Browse lists saved
  `ir_NNNN.ir` captures first (newest first), other `*.ir` after them by name.
  `check_emu_image.py` needed no change — the seed is partition content, not a code
  difference, and the check still passes.
* **No separate detail screen.** Each list row shows the decoded payload
  (`NEC 00/12`, `RAW 13`, `(invalid)`); OK opens the confirm dialog, which is the detail
  view: `"NEC 00/12, 38000 Hz, 67 pulses, 68 ms. Own devices only."` The same text is
  logged (`irapp: confirm <file>: …`) before the dialog opens, so the proposed
  transmission is auditable.
* **At most 16 files** are listed (`IRAPP_BROWSE_MAX`), each with its 24-byte name and
  description. Browse and send add 1 014 B of static RAM (list 784 B, menu rows 192 B,
  the sent file's name 24 B), within the 1 kB the 64 × 16 B budget allowed, because they
  reuse the save path's buffers instead of
  their own: one `irapp_work` (parsed signal + `.ir` text, 2 332 B) serves whichever worker
  runs, and Save is refused (`save refused: busy`) while a list or load holds it. Without
  the sharing the app grew by 2.3 kB more (measured, `nm`).
* BACK on the list returns to the receive view. Re-initialising the one menu for the
  other mode wiped the `on_exit` hook the app manager places on the app screen — BACK
  then popped the IR app without stopping it, leaving RX on. The emulator scenario caught
  it; the hook is carried across, and both the host test and `v03-replay` now assert
  `app: stop ir` after a browse round trip.
* The host UI port gained two opt-in seams (both off by default, so other UI tests are
  unchanged): an inline worker and an in-memory `captures/` directory.

**Out of scope.** Deleting/renaming files, sending lists, scheduled or repeated sends,
editing timings, importing other formats.

**Resource limits.** Browse keeps names only (≤ 64 × 16 B); one parsed signal at a time.

**CI.** Host tests, one storage-image scenario, seeded partition. **Security.** Every
send is confirmed, rate-limited and logged; the dialog says "own devices only"
(SECURITY_SCOPE §2). **Physical HIL (deferred to V1).** Replay of a captured own-remote
signal through the LED with the own device responding; raw replay of an unsupported
own device.

---

## STARK-0307 — V0.3 exit scenarios, soak, measurements, V1 IR bring-up checklist

**Goal.** Every V0.3 exit criterion's software half is demonstrated in CI and recorded;
every physical half is written down as a V1 checklist, unclaimed.

**Layer.** Tests, CI, docs.

**Files.** `test/emu/storage/v03-protocols.toml`, `test/emu/storage/v03-capture-replay.toml`,
`test/emu/soak/v03-soak.toml`, `.github/workflows/ci.yml` (soak job), `docs/bringup/v1-ir.md`
(new), `docs/measurements.md`, `docs/VALIDATION.md` §3.6, `README.md`, ROADMAP (status),
TESTING §4/§5.

**Deps.** STARK-0306.

**Notes.**
* `storage/v03-protocols.toml`: every seeded protocol file replayed and received back
  with identical fields (exit 1, software half — the loopback half).
* `storage/v03-capture-replay.toml`: the exit 2/3 flow end to end, incl. the RAW signal.
* `soak/v03-soak.toml` (storage image, `rmt_loopback`): 60 s — IR app open with RX on,
  ten replays across protocols (respecting the interval), saves, browse, App Test
  self-test once, V0.2 activity (a write test, settings), idle at the root; `drops=0`,
  heap delta ≤ 1 kB.
* `docs/bringup/v1-ir.md`: the physical checklist from ROADMAP V1 exit 9 with expected
  values and blank result columns (carrier per protocol, duty, LED current, receiver
  output, ≥ 20 recorded vectors per protocol, own-device response, range/angle/ambient,
  echo, GPIO 47/48).
* `docs/measurements.md`: V0.3 rows (heap at boot, IR static RAM, flash, task stack HWMs,
  soak heap delta, codec coverage) and a V0.3 exit table with software evidence and
  **physical: deferred to V1**; V1 hardware table gains the IR rows.
* README status → "V0.3 complete (software/protocol-verified; optical IR at V1)".

**AC.**
1. Every V0.3 exit criterion's software half met with evidence; every physical half
   listed, none claimed; `docs/bringup/v1-ir.md` exists with blank results.
2. All V0.3 scenarios and the three soaks pass in CI; `drops=0`.
3. No simulator names in production code; no scenario asserts a wall-clock value.
4. No document describes the loopback as optical evidence (grep for "optical" reviewed).

**As built.**
* `storage/v03-protocols.toml` replays the eight seeded files (seven protocol variants
  and the RAW signal); each `ir: rx` carries the sent fields. `forbid_between` keeps any
  `ir: tx` from appearing before the first confirmation.
* `storage/v03-capture-replay.toml` has no remote to capture from, so a seeded RC6 replay
  stands in for one: its capture is saved (`ir_0001.ir`), listed first, parsed back
  (the logged confirm text names the same fields), replayed and captured again. RAW goes
  through the same steps as `ir_0002.ir`, with the same 13 durations.
* `soak/v03-soak.toml` passes with root heap Δ 44 B. The CI soak job gained one step.
* Stack headroom was measured the way STARK-0206 did it: a GDB fill-pattern dump after
  every protocol was sent and received, saves included. The IR task leaves 1 152 of
  3 072 B untouched and the app worker 2 356 of 4 096 B.
* **Static RAM corrections, recorded where they belong:** STARK-0305's app holds
  5 984 B, not ≤ 4 kB, and STARK-0306's browse and send add 1 014 B after sharing the worker
  buffer. See those tasks' "As built" notes and docs/measurements.md.
* **AC3 correction:** the simulator-name grep was not empty after STARK-0303/0304. Two
  comments named the loopback; they were reworded in STARK-0304 (PR akin941/Stark-One#46),
  and the grep is now empty.
* `docs/bringup/v1-ir.md` lists 17 checks with every result blank. Check 12, the self-test
  with the LED facing the board's own receiver, would be the first optical end-to-end
  evidence.

**Out of scope.** Hardware work; V0.4.

**CI.** The soak job runs the V0.3 soak too. **Security.** None new. **Physical HIL
(deferred to V1).** `docs/bringup/v1-ir.md` in full.

---

# V0.4 — NFC lab

**Scope source:** [ROADMAP.md](ROADMAP.md) "V0.4". Nothing from V0.5+ is pulled forward
(no module discovery, no capability bits, no `caps_required` — ARCHITECTURE §6.9). Tags:
ISO 14443A at 106 kbit/s through a PN532 over I²C. NDEF read and write only on NFC
Forum Type 2 tags (NTAG213/215/216, MIFARE Ultralight family). MIFARE Classic and
ISO-DEP tags are identified only, from ATQA/SAK/UID. No SPI fallback, no ISO 14443B,
FeliCa or ISO 15693. No key recovery, key dictionaries, UID writing, cloning, emulation
or dump restore (SECURITY_SCOPE §3, ADR-0020 §5).

**Completion levels (ADR-0020, owner decision 2026-10-25).** *V0.4 complete* means NFC
**software / protocol verified**. It never means a real tag was read or written. Every
task lists its software evidence and its **physical HIL evidence (deferred to V1)**,
collected in ROADMAP V1 exit 10, the V1 hardware table and `docs/bringup/v1-nfc.md`.

**Validation layers** (each task uses the narrowest one that proves its claim):

* **Host (Tier 1).** The pure NFC core: NDEF, the PN532 frame codec, the PN532 protocol
  driver, Type 2 operations, tag identification, the poll state machine, the read and
  write flows, and the dump format. Tests use committed **spec-derived vectors**
  (`test/host/vectors/nfc_*.h`, generated by `scripts/gen_nfc_vectors.py` from UM0701-02,
  the NFC Forum Type 2 Tag and NDEF/RTD specifications and the NTAG21x data sheet;
  ADR-0020 §4).
* **Host, fake PN532.** The production driver and read/write flows run against
  `test/fake/fake_pn532.c`, which answers I²C transactions the way the chip does,
  holds a simulated tag's memory, and injects faults (ADR-0020 §3).
* **Host UI port (Tier 4B).** The NFC app's screens, inspected goldens, with a fake
  `stark_nfc`.
* **Emulator, production image (Tier 3).** Boot on an **empty** I²C bus: an address
  NACK, `nfc: no module`, the app showing it and polling nothing. No PN532 is in this
  path.
* **Emulator, test image.** The storage test image also substitutes
  `stark_nfc_transport` with one built on the fake (ADR-0020 §3). GDB pokes choose the
  tag (`emu_nfc_tag`) and inject faults (`emu_nfc_fault`). The production driver, NFC
  task, app, UI and storage run unchanged. This is integration evidence, never RF
  evidence.

**Hardware.** None. `I2C_SDA` = GPIO 1 and `I2C_SCL` = GPIO 2 are already in
`stark_board` (HARDWARE.md §2.2). The PN532 answers at 7-bit address 0x24.

**Exit-criteria map** (ROADMAP V0.4; each row's V1 half is ROADMAP V1 exit 10):

| Exit criterion | V0.4 software evidence | Proven by |
| --- | --- | --- |
| 1. UID and technology of own tags, ≥ 20/20 reads | ≥ 20 tag vectors identified through the production driver and the fake; 20/20 consecutive reads with no stale state, host and emulator test image | STARK-0402, STARK-0403 (host), STARK-0405 (`storage/v04-reads.toml`) |
| 2. Write and read back an NDEF URI record | URI message written to the fake's NTAG memory, read back byte-identical and parsed; refusals for read-only, too small and wrong tag | STARK-0401, STARK-0403, STARK-0404, STARK-0405 (`storage/v04-write-readback.toml`) |
| 3. Module absence detected, app disables itself | empty bus on the production image; absent, silent, NACKing and corrupt modules and recovery on the host | STARK-0402, STARK-0404 (`v04-nfc-absent.toml`) |

**System event id** (ARCHITECTURE §6.3): 5 `STARK_SYS_NFC` (`arg` = `STARK_NFC_EVT_TAG`
1, a tag was read; `STARK_NFC_EVT_LEFT` 2; `STARK_NFC_EVT_MODULE` 3, the module state
changed; `STARK_NFC_EVT_WRITE_DONE` 4; `STARK_NFC_EVT_WRITE_FAILED` 5;
`STARK_NFC_EVT_DUMP_DONE` 6; `STARK_NFC_EVT_DUMP_FAILED` 7).

**Launcher.** The NFC app joins **Lab**, registered before IR, so the IR app keeps its
UP-from-About path (Storage, Settings, IR). Rows: Lab header 10, NFC 11, IR 12, Device
header 13, Settings 14, Storage 15. Scenarios update only `sel=` numbers (STARK-0404).

**Common definitions** (`components/stark_nfc/include/nfc_types.h`, pure, used by every
task):

```c
#define NFC_UID_MAX       10   /* ISO 14443A: 4, 7 or 10 bytes */
#define NFC_T2_PAGE_BYTES 4
#define NFC_T2_MAX_PAGES  231  /* NTAG216: 0..230 */
#define NFC_NDEF_MAX      872  /* the largest Type 2 data area we support: NTAG216, CC size 0x6D x 8 */
typedef enum {
    NFC_TECH_UNKNOWN = 0, NFC_TECH_NTAG213, NFC_TECH_NTAG215, NFC_TECH_NTAG216,
    NFC_TECH_ULTRALIGHT, NFC_TECH_TYPE2,      /* other Type 2 */
    NFC_TECH_CLASSIC_MINI, NFC_TECH_CLASSIC_1K, NFC_TECH_CLASSIC_4K,
    NFC_TECH_ISO_DEP, NFC_TECH_COUNT
} nfc_tech_t;
typedef struct {
    uint8_t uid[NFC_UID_MAX];
    uint8_t uid_len;
    uint16_t atqa;            /* SENS_RES, as sent (MSB first) */
    uint8_t sak;              /* SEL_RES */
    nfc_tech_t tech;
    uint16_t pages;           /* Type 2: total pages; 0 otherwise */
} nfc_tag_t;
```

---

## STARK-0401 — NDEF parse/serialise core + host tests

**Goal.** NDEF messages are decoded and encoded exactly as the NFC Forum specifies.
Malformed input is rejected, unsupported record types pass through safely, and nothing
reads past its buffer.

**Layer.** L2 pure core in `stark_nfc` (component created here; no ESP-IDF code yet).

**Files.** `components/stark_nfc/{ndef.c,include/ndef.h,include/nfc_types.h,CMakeLists.txt}`,
`scripts/gen_nfc_vectors.py` (new), `scripts/test_gen_nfc_vectors.py`,
`test/host/vectors/nfc_ndef_vectors.h` (generated), `test/host/test_ndef.c`,
`test/host/CMakeLists.txt`, `scripts/check_layers.py` (`stark_nfc` at L3, its pure files
checked as L2 like `stark_ir`), `.github/workflows/ci.yml` (lint:
`gen_nfc_vectors.py --check`), `docs/nfc-format.md` (new: the NDEF subset).

**Deps.** None in V0.4 (V0.3 complete).

**API.**

```c
#define NDEF_MAX_RECORDS 8
typedef enum { NDEF_TNF_EMPTY = 0, NDEF_TNF_WELL_KNOWN, NDEF_TNF_MIME, NDEF_TNF_URI,
               NDEF_TNF_EXTERNAL, NDEF_TNF_UNKNOWN, NDEF_TNF_UNCHANGED } ndef_tnf_t;
typedef struct {                /* views into the caller's message buffer */
    ndef_tnf_t tnf;
    const uint8_t *type; uint8_t type_len;
    const uint8_t *id; uint8_t id_len;
    const uint8_t *payload; uint32_t payload_len;
} ndef_record_t;
typedef struct { ndef_record_t rec[NDEF_MAX_RECORDS]; uint8_t count; } ndef_message_t;

stark_err_t ndef_parse(const uint8_t *buf, size_t len, ndef_message_t *out);
size_t ndef_encode(const ndef_record_t *recs, size_t n, uint8_t *out, size_t cap); /* 0: error */
/* RTD URI ("U"): prefix codes 0x00-0x23 */
stark_err_t ndef_uri_get(const ndef_record_t *r, char *out, size_t cap);
stark_err_t ndef_uri_make(const char *uri, uint8_t *payload, size_t cap, ndef_record_t *out);
/* RTD Text ("T"): UTF-8 only */
stark_err_t ndef_text_get(const ndef_record_t *r, char *lang, size_t lcap, char *text, size_t tcap);
stark_err_t ndef_text_make(const char *lang, const char *text, uint8_t *payload, size_t cap,
                           ndef_record_t *out);
/* "URI https://…", "Text en: …", "MIME text/plain 12 B", "External a.b:c 3 B", "Empty",
 * "Unknown 4 B"; truncated to cap with "…" */
void ndef_describe(const ndef_record_t *r, char *out, size_t cap);
```

**Notes (the rules).**
* Header byte: MB, ME, CF, SR, IL, TNF. The first record has MB set, the last has ME
  set, and nothing follows ME. SR records have a 1-byte payload length; otherwise it is
  4 bytes and must be ≤ `NFC_NDEF_MAX`. IL adds an ID length and ID.
* TNF Empty requires type, ID and payload lengths of 0. TNF Unknown and Unchanged
  require a type length of 0. TNF 7 is reserved and rejected. CF (chunked records) and
  TNF Unchanged are `STARK_ERR_NOT_SUPPORTED`: rejected safely, never half-parsed.
* More than `NDEF_MAX_RECORDS` records is `STARK_ERR_NO_SPACE`. Any length that points
  past `len` is `STARK_ERR_INVALID_ARG`.
* MIME, External, Smart Poster (`Sp`) and other well-known types parse as generic
  records and are described but not interpreted (no nested parsing).
* URI prefix codes above 0x23 are invalid. Text records with the UTF-16 bit set give
  `STARK_ERR_NOT_SUPPORTED`. A language code must be 1–63 bytes.
* `ndef_encode` sets MB/ME and chooses SR whenever payload ≤ 255. It is the exact
  inverse of `ndef_parse` for every supported record.
* No allocation. Every output is bounded by its `cap`, and strings are NUL-terminated.

**Host tests.** `test_ndef.c` over generated vectors (an independent Python encoder in
`gen_nfc_vectors.py`):
* every URI prefix code;
* Text in English and Turkish UTF-8;
* 1-, 2- and 3-record messages mixing URI, Text, MIME and External;
* an empty record;
* boundary payloads of 255 (SR) and 256 (long) bytes;
* a long record of 800 bytes;
* the maximum record count, and one more.

Malformed vectors cover:
* missing MB, missing ME, bytes after ME;
* payload, type and ID lengths that overflow;
* truncation at every byte of a 3-record message;
* CF set, TNF 7, TNF Empty with lengths;
* UTF-16 text, prefix 0x24, an empty or oversized language code.

Also: encode → parse → encode is byte-identical for every valid vector; 10 000 seeded
random mutations of valid messages parse or fail cleanly under ASan/UBSan; and
`ndef_describe` truncates without overrunning.

**Fake-PN532 tests.** None (pure format).

**Emulator.** None.

**Fault injection.** Truncation at every offset, oversized lengths, and fuzzing (above).

**AC.**
1. Every valid vector parses to the expected fields and re-encodes byte-identically;
   every malformed vector is rejected with the documented error.
2. Fuzzing is clean under ASan/UBSan; no read past `len` (sanitizer-proven).
3. `ndef.c` has 100 % line coverage; `docs/nfc-format.md` documents the subset.
4. `gen_nfc_vectors.py --check` runs in CI lint.

**Resource limits.** `ndef.c` ≤ 4 kB flash; no allocation; `ndef_message_t` ≤ 200 B.

**Out of scope.** Chunked records, nested Smart Poster parsing, UTF-16 text, signature
records, Handover/Wi-Fi/vCard interpretation.

**CI.** Host suite, lint regeneration check. **Security.** The parser reads data from
tags, so it is strict, bounded and fuzzed. **Physical HIL (deferred to V1).** NDEF
messages written by a phone to our own tag decode the same on the device.

---

## STARK-0402 — PN532 driver over I²C: framing, transport, module detection

**Goal.** The production firmware talks to a PN532 the way UM0701-02 specifies:
- framed commands, ACK/NACK, checksummed responses and the I²C ready byte;
- bounded timeouts and retries;
- detection of a missing, silent or unexpected module.

Every path is exercised against the fake PN532.

**Layer.** L2 pure cores (`pn532_frame.c`, `pn532.c`) in `stark_nfc`; L1 port
`stark_nfc_transport` (new); L3 `stark_nfc.c` (init and module detection only).

**Files.**
- `components/stark_nfc/{pn532_frame.{c,h},pn532.{c,h},stark_nfc.c,include/stark_nfc.h,CMakeLists.txt}`
- `components/stark_nfc_transport/{include/stark_nfc_transport.h,nfc_transport_i2c.c,CMakeLists.txt}` (new)
- `components/stark_board/{include/stark_board.h,board_devkitc1.c}` (create the I²C master bus at init, `stark_board_i2c_bus()`)
- `main/main.c` (`stark_nfc_init()` after `stark_ir_init()`), `main/Kconfig.stark` (I²C clock, timeouts, task)
- `test/fake/{fake_pn532.c,fake_pn532.h}` (new)
- `scripts/gen_nfc_vectors.py` (PN532 frames), `test/host/vectors/nfc_pn532_vectors.h`
- `test/host/{test_pn532_frame.c,test_pn532.c,CMakeLists.txt}`
- `test/emu/storage_image/components/stark_nfc_transport/{nfc_transport_fake.c,CMakeLists.txt}` (new)
- `scripts/check_emu_image.py` (+ test: two allowed substitutions)
- `scripts/test_emu.py` (POKE_VARS += `emu_nfc_tag`, `emu_nfc_fault`)
- `scripts/check_layers.py` (`stark_nfc_transport` at L1)
- `test/emu/v04-nfc-boot.toml`, `test/emu/storage/v04-nfc-module.toml`
- ARCHITECTURE §6.3, §6.9e, §7; VALIDATION §3.7

**Deps.** STARK-0401.

**API.**

```c
/* stark_nfc_transport.h — L1, the only code that touches the bus */
stark_err_t stark_nfc_transport_open(void);   /* device 0x24 on stark_board_i2c_bus() */
void        stark_nfc_transport_close(void);
stark_err_t stark_nfc_transport_write(const uint8_t *buf, size_t len, uint32_t timeout_ms);
stark_err_t stark_nfc_transport_read(uint8_t *buf, size_t len, uint32_t timeout_ms);
/* errors: address NACK -> STARK_ERR_NOT_FOUND, bus timeout -> STARK_ERR_TIMEOUT, other -> STARK_ERR_IO;
 * STARK_ERR_NOT_SUPPORTED when the board has no I2C pins */

/* pn532_frame.h — pure */
#define PN532_DATA_MAX 254      /* TFI + PD of a normal information frame */
size_t pn532_frame_build(uint8_t cmd, const uint8_t *params, size_t n, uint8_t *out, size_t cap);
typedef enum { PN532_RX_ACK, PN532_RX_NACK, PN532_RX_DATA, PN532_RX_APP_ERROR } pn532_rx_t;
stark_err_t pn532_frame_parse(const uint8_t *buf, size_t len, pn532_rx_t *kind,
                              const uint8_t **data, size_t *dlen);
extern const uint8_t PN532_ACK[6], PN532_NACK[6];

/* pn532.h — pure protocol driver over an injected transport */
typedef struct {
    stark_err_t (*write)(void *ctx, const uint8_t *buf, size_t len);
    stark_err_t (*read)(void *ctx, uint8_t *buf, size_t len); /* one read: status byte first */
    uint32_t (*now_ms)(void *ctx);
    void (*delay_ms)(void *ctx, uint32_t ms);
    void *ctx;
} pn532_io_t;
typedef struct { uint32_t retries, naks_sent, checksum_errors, timeouts, bus_errors; } pn532_stats_t;
typedef struct { pn532_io_t io; pn532_stats_t stats; uint8_t buf[PN532_DATA_MAX + 9]; } pn532_t;

void        pn532_bind(pn532_t *d, const pn532_io_t *io);
stark_err_t pn532_firmware(pn532_t *d, uint8_t *ic, uint8_t *ver, uint8_t *rev);
stark_err_t pn532_sam_normal(pn532_t *d);               /* SAMConfiguration 01 14 01 */
stark_err_t pn532_field(pn532_t *d, bool on);           /* RFConfiguration item 1 */
stark_err_t pn532_passive_retries(pn532_t *d, uint8_t n); /* RFConfiguration item 5 */
stark_err_t pn532_command(pn532_t *d, uint8_t cmd, const uint8_t *params, size_t n,
                          uint8_t *resp, size_t cap, size_t *got, uint32_t timeout_ms);

/* stark_nfc.h — this task: init and module state only */
typedef enum { STARK_NFC_NOT_FITTED, STARK_NFC_ABSENT, STARK_NFC_READY, STARK_NFC_LOST } stark_nfc_module_t;
stark_err_t        stark_nfc_init(void);   /* opens the transport, detects the module, field off */
stark_nfc_module_t stark_nfc_module(void);
```

**Protocol (UM0701-02 §6.2, §6.3.4).**
* Host frame: `00 00 FF LEN LCS D4 CMD PD… DCS 00`, where `LEN + LCS ≡ 0` and the sum
  of TFI, CMD, PD and DCS ≡ 0 (mod 256).
* I²C read: the first byte is the status byte; bit 0 set means the chip is ready and
  the frame follows in the same read.
* A transaction runs: write the frame, poll the ready byte for up to `ack_timeout`,
  read the ACK `00 00 FF 00 FF 00`, poll ready for up to the command's timeout, then
  read status plus a frame of up to `PN532_DATA_MAX + 7` bytes. The response must have
  TFI `D5` and code `CMD + 1`.
* Failures:
  - A NACK or bad ACK repeats the command, up to `STARK_NFC_RETRIES` (2).
  - A response with a bad LCS or DCS makes the host send `PN532_NACK`, asking for the
    response again, up to 2 times; then `STARK_ERR_IO`.
  - The application-error frame `00 00 FF 01 FF 7F 81 00` gives `STARK_ERR_IO`.
  - Extended frames (`FF FF`) give `STARK_ERR_NOT_SUPPORTED`.
  - A wrong TFI or response code gives `STARK_ERR_IO`.
  - A chip that never becomes ready gives `STARK_ERR_TIMEOUT`.
* Every failure updates `pn532_stats_t`.
* Detection: `GetFirmwareVersion` (IC must be `0x32`), then `SAMConfiguration` normal
  mode, `RFConfiguration` passive retries = 1 (so a poll returns at once with no tag),
  field off. Results:
  - Logs `nfc: pn532 fw <ver>.<rev>`.
  - An address NACK, or a timeout after retries, logs `nfc: no module (<err>)` (state
    ABSENT).
  - Another IC logs `nfc: unexpected chip 0x<ic>` (ABSENT).
  - No I²C pins logs `nfc: not fitted`.

**Fake PN532** (`test/fake/fake_pn532.h`, pure C, also linked by the test image):

```c
typedef struct {                      /* a simulated tag, spec-derived */
    uint8_t uid[10]; uint8_t uid_len; uint16_t atqa; uint8_t sak;
    bool type2; uint8_t version[8];   /* GET_VERSION reply, Type 2 only */
    uint16_t pages; uint8_t mem[NFC_T2_MAX_PAGES * 4];
    uint16_t ro_from;                 /* pages >= this ignore WRITE (lock emulation) */
} fake_tag_t;
typedef enum { FAKE_NONE, FAKE_NO_DEVICE, FAKE_BUS_ERROR, FAKE_NEVER_READY, FAKE_SEND_NACK,
               FAKE_BAD_ACK, FAKE_BAD_LCS, FAKE_BAD_DCS, FAKE_TRUNCATED, FAKE_WRONG_CODE,
               FAKE_APP_ERROR, FAKE_EXTENDED, FAKE_OTHER_IC } fake_fault_t;
void fake_pn532_reset(void);                                  /* power-on state */
void fake_pn532_fault(fake_fault_t f, int times);              /* -1: until cleared */
void fake_pn532_tag(const fake_tag_t *t);                      /* NULL: field empty */
void fake_pn532_vanish_after(unsigned exchanges);              /* tag leaves mid-transaction */
stark_err_t fake_pn532_write(const uint8_t *buf, size_t len);  /* transport-shaped */
stark_err_t fake_pn532_read(uint8_t *buf, size_t len);
const fake_pn532_stats_t *fake_pn532_stats(void);              /* frames seen, field on/off, NACKs */
```

The fake:
- validates every host frame (bad checksum → it stays silent, like the chip);
- tracks the ready state per transaction and honours a host NACK by resending;
- implements `GetFirmwareVersion` (IC 0x32, fw 1.6), `SAMConfiguration`,
  `RFConfiguration` items 1 and 5, `InListPassiveTarget` (1 target, 106 kbit/s A),
  `InDataExchange` and `InRelease`;
- for Type 2 tags, implements READ (0x30, 16 bytes with roll-over), WRITE (0xA2) and
  GET_VERSION (0x60);
- returns status 0x01 (target timeout) once the tag has vanished.

The test image's transport exposes `volatile uint8_t emu_nfc_tag, emu_nfc_fault`, which
select an entry from a fixed catalogue:

| `emu_nfc_tag` | Tag in the field |
| --- | --- |
| 0 | none |
| 1 | NTAG213 with an NDEF URI |
| 2 | blank NTAG215 |
| 3 | MIFARE Classic 1K |
| 4 | read-only NTAG213 |
| 5 | Ultralight |
| 6 | ISO-DEP |
| 7 | NTAG213 that vanishes mid-read |

**Host tests.**
- `test_pn532_frame.c`:
  - generated vectors: UM0701 frames for every command used, ACK/NACK/app-error;
  - each byte of a valid frame corrupted;
  - truncation at every offset, LEN/LCS mismatch, DCS errors, extended frame, empty input;
  - build → parse round trip for params of 0–254 bytes.
- `test_pn532.c` against the fake, one case per listed behaviour:
  - valid ACK and response;
  - PN532 NACK → retried then OK;
  - NACK every time → `STARK_ERR_IO` after 2 retries;
  - bad ACK;
  - bad LCS/DCS → host NACK → resent response accepted;
  - persistent checksum error → `IO`;
  - truncated response;
  - never ready → `TIMEOUT` with the configured timeout honoured;
  - I²C bus error;
  - address NACK → `NOT_FOUND`;
  - wrong response code;
  - app-error frame;
  - extended frame;
  - other IC;
  - re-detection after a fault clears (recovery);
  - 100 consecutive firmware queries with no stats drift.
- Fake time advances only through `delay_ms` (deterministic, no sleeps).

**Emulator.**
- `v04-nfc-boot.toml` (production image, empty bus): `nfc: no module (Not found)` after
  the I²C bus comes up, boot continues to `boot: ui_ready`, and every earlier scenario
  passes unchanged.
- `storage/v04-nfc-module.toml` (test image with the fake): `nfc: pn532 fw 1.6`.
  Detection runs at boot, before the harness's first breakpoint, so a fake-side absent
  module cannot be poked in time. The emulator's absence evidence is the production
  image's empty bus; the fake's absent, silent and corrupt modules are host-proven.

**Fault injection.** Every `fake_fault_t` on every command the driver issues (host),
counted in `pn532_stats_t`.

**AC.**
1. `pn532_frame.c` and `pn532.c` have 100 % line coverage; every fault listed above is
   tested against the fake with its documented error.
2. The production image boots on the empty emulated bus to `nfc: no module`, with no
   hang, panic or retry storm, and boot heap stays ≥ 200 kB.
3. `check_emu_image.py` passes: the test image differs only in `stark_storage_medium`
   and `stark_nfc_transport`. `grep -rIiE 'wokwi|qemu|esp_emu|esp-emu|loopback|fake_pn532|emu_nfc'
   components/ apps/ main/` is empty (comments describing host-test fakes are allowed,
   as elsewhere; no production code may reference the fake device or its switches).
4. I²C pins are named only in `stark_board`; layers check passes; ARCHITECTURE
   documents the transport, the driver and the event id.

**Resource limits.** pn532 cores ≤ 6 kB flash, `pn532_t` ≤ 300 B; the transport ≤ 2 kB
flash plus the ESP-IDF I²C driver; boot heap drop ≤ 4 kB.

**Out of scope.** SPI and UART PN532 transports, the IRQ and RSTO pins (polling only),
low-power modes, ISO 14443B/FeliCa, `TgInitAsTarget` (emulation), firmware updates.

**CI.** Host suite, both images, harness tests. **Security.** No command beyond those
listed is built; the API exposes no raw-command passthrough to apps. **Physical HIL
(deferred to V1).** PN532 at 0x24 answering on GPIO 1/2 with the board's pull-ups;
I²C waveform on a logic analyser; detection and initialisation time; module unplugged
at boot.

---

## STARK-0403 — Tag detection, identification and Type 2 read (state machine + NFC task)

**Goal.** While an app asks for it, the NFC task polls for a tag, identifies it, reads
a Type 2 tag's capability container and NDEF message, and reports arrival, removal and
module loss through events. The decisions are made by pure, host-tested code.

**Layer.** L2 pure cores (`nfc_ident.c`, `nfc_type2.c`, `nfc_reader.c`, `nfc_poll.c`)
and the L3 NFC task in `stark_nfc.c`.

**Files.**
- `components/stark_nfc/{nfc_ident.{c,h},nfc_type2.{c,h},nfc_reader.{c,h},nfc_poll.{c,h},stark_nfc.c,include/stark_nfc.h}`
- `main/Kconfig.stark` (poll period, task)
- `scripts/gen_nfc_vectors.py` (tag catalogue), `test/host/vectors/nfc_tag_vectors.h`
- `test/host/{test_nfc_ident.c,test_nfc_type2.c,test_nfc_reader.c,test_nfc_poll.c}`
- ARCHITECTURE §6.9e

**Deps.** STARK-0402.

**API (added to `stark_nfc.h`).**

```c
typedef struct {
    nfc_tag_t tag;
    stark_err_t read_err;        /* STARK_OK, or why the NDEF read stopped */
    bool ndef_present;           /* Type 2 with a valid CC and an NDEF TLV */
    bool writable;               /* CC access 0x00 */
    uint16_t ndef_capacity;      /* bytes the CC allows for the NDEF TLV value */
    uint16_t ndef_len;
    uint8_t ndef[NFC_NDEF_MAX];
} stark_nfc_read_t;
stark_err_t stark_nfc_start(void);   /* field on, polling every STARK_NFC_POLL_MS */
stark_err_t stark_nfc_stop(void);    /* field off, polling stops, tag forgotten */
stark_err_t stark_nfc_take(stark_nfc_read_t *out); /* the current tag's snapshot */
```

**Notes.**
* **Identification** (`nfc_ident.c`, NXP AN10833 and the NTAG21x data sheet):
  - SAK 0x00 is Type 2; GET_VERSION then tells NTAG213/215/216 apart by storage byte
    0x0F/0x11/0x13. Ultralight (EV1) has product type 0x03. No GET_VERSION reply means
    plain Ultralight or another Type 2.
  - SAK 0x09 is Classic Mini, 0x08 Classic 1K and 0x18 Classic 4K: identified only.
  - SAK bit 0x20 is ISO-DEP: identified only.
  - Anything else is UNKNOWN, shown with its raw ATQA/SAK.
* **Type 2** (`nfc_type2.c`, NFC Forum Type 2 Tag §2–§4):
  - READ returns 4 pages.
  - CC (page 3): `E1`, version major 1, size byte × 8 = data-area bytes, access 0x00
    read/write or 0x0F read-only. Any other value means "no NDEF" (not an error).
  - TLV walk over the data area: NULL 00, Lock Control 01 and Memory Control 02
    (skipped by their length), NDEF 03 (1-byte length, or FF + 2 bytes), Terminator FE,
    unknown types skipped by length. The walk stops at the data-area end.
  - An NDEF length beyond the data area is `STARK_ERR_INVALID_ARG`.
  - Read only as many pages as the TLV needs.
* **Read flow** (`nfc_reader.c`, pure, over `pn532_t`): `InListPassiveTarget` → ident
  (+ GET_VERSION for SAK 0x00) → for Type 2: CC, TLV, NDEF pages → `InRelease`. A target
  timeout mid-read (the tag left) ends the read with `read_err = STARK_ERR_NOT_FOUND`
  and keeps the identification; nothing from a previous tag survives into the snapshot.
* **Poll state machine** (`nfc_poll.c`, pure):
  - States: IDLE, PRESENT(uid), RECOVERING, ABSENT.
  - A tag with a new UID gives TAG (read it); the same UID while present gives nothing.
  - Two consecutive empty polls give LEFT, so a tag is not lost to one missed poll.
  - Three consecutive transport errors give MODULE (lost) → RECOVERING, which
    re-detects every `STARK_NFC_RECOVER_MS`; success gives MODULE (ready).
* **Task** (`stark_nfc`, prio 3, 4 kB, core 0): runs only between `start` and `stop`.
  The field is on only while polling (receive-first, ADR-0020 §5). Logs:
  - `nfc: field on` / `nfc: field off`;
  - `nfc: tag <tech> uid=<hex> atqa=<hex> sak=<hex>`;
  - `nfc: ndef <n> B` / `nfc: no ndef` / `nfc: read stopped: <err>`;
  - `nfc: tag left`;
  - `nfc: module lost` / `nfc: module back`.
  Each is published as `STARK_SYS_NFC`.

**Host tests.**
- `test_nfc_ident.c`: ≥ 20 generated tag vectors:
  - 4-byte UIDs (Classic) and 7-byte UIDs (NTAG);
  - NTAG213/215/216, Ultralight, Ultralight EV1, Classic Mini/1K/4K, ISO-DEP;
  - unknown SAKs.
- `test_nfc_type2.c`:
  - CC variants: valid, read-only, wrong magic, version 2, zero size;
  - TLV layouts: NDEF first, after Lock/Memory Control, after NULLs, long-form length,
    no terminator, unknown TLV, NDEF past the data area, empty NDEF TLV.
- `test_nfc_reader.c` (production reader + driver + fake):
  - every catalogue tag read and identified;
  - 20/20 consecutive reads of one tag, identical snapshots;
  - the tag vanishing after each possible exchange: identification kept, `read_err`,
    no stale NDEF;
  - a different tag after removal shows no residue of the first;
  - checksum errors and NACKs mid-read recovered;
  - a timeout mid-read.
- `test_nfc_poll.c`: arrival, same tag repeated, removal debounce (one miss is not
  LEFT), flapping, a new tag without a gap, module lost after 3 errors, recovery
  retries and back.

**Emulator.** The NFC task starts only through `stark_nfc_start`, and the app that
calls it is STARK-0404, so emulator detection scenarios land there. This task adds no
scenario of its own; its emulator effect is that every earlier scenario passes
unchanged (polling off ⇒ field off, no I²C traffic after init).

**Fault injection.** Mid-read vanish at every exchange, faults during polling, and
module loss and return (host). The emulator exercises the same through `emu_nfc_fault`
from STARK-0404.

**AC.**
1. ≥ 20 tag vectors identified correctly; 20/20 consecutive reads with identical
   snapshots (host).
2. Every removal, flap and recovery sequence produces the documented events; a vanished
   tag never leaves stale data.
3. `nfc_ident.c`, `nfc_type2.c`, `nfc_reader.c` and `nfc_poll.c` have 100 % line
   coverage.
4. Polling and the field are off unless started; earlier scenarios unchanged.

**Resource limits.** Snapshot ≤ 1 kB, task stack 4 kB, NFC static total ≤ 8 kB; flash
≤ +12 kB.

**Out of scope.** Several tags at once (the first target only), anti-collision tuning,
Classic sector reads, ISO-DEP APDUs, NTAG counters/signature/password commands.

**CI.** Host suite. **Security.** No authentication command is built; Classic and
ISO-DEP stop at identification. **Physical HIL (deferred to V1).** Detection range and
orientation, removal latency, weak and noisy tags, two tags in the field, ≥ 20/20 real
reads.

---

## STARK-0404 — NFC app: tag view, NDEF viewer, confirmed NDEF write

**Goal.** The NFC app shows the tag that is present (technology, UID, ATQA/SAK, size)
and its NDEF records. It writes a chosen NDEF URI record to our own writable Type 2 tag
after a confirmation, then reads it back. When the module is absent it says so and does
nothing.

**Layer.** L5 app; L3 additions (write request) in `stark_nfc`; composition root.

**Files.**
- `apps/app_nfc/{app_nfc.c,nfc_view.c,nfc_write.c,nfcapp.h,CMakeLists.txt}` (new; each
  ≤ 150 lines)
- `components/stark_nfc/{nfc_writer.{c,h},stark_nfc.c,include/stark_nfc.h}`
- `main/app_registry.{c,h}` (NFC before IR)
- `test/host/{test_nfc_writer.c,test_ui_nfc.c,port/ui_port_nfc.{c,h},CMakeLists.txt}`
- `test/emu/v04-nfc-absent.toml`, `test/emu/storage/v04-nfc-app.toml`
- existing scenarios' `sel=` lines; ARCHITECTURE §6.7, §6.9e; TESTING §4

**Deps.** STARK-0403.

**API (added to `stark_nfc.h`).**

```c
/* Writes msg as the tag's NDEF TLV, only to the tag whose UID is given, then reads it back.
 * Returns at once; STARK_SYS_NFC WRITE_DONE / WRITE_FAILED follows. */
stark_err_t stark_nfc_write_ndef(const uint8_t *uid, uint8_t uid_len,
                                 const uint8_t *msg, size_t len);
```

**Notes.**
* **Screen:** one menu.
  - Module present: a status row ("Hold a tag near" / technology), UID, ATQA/SAK, size,
    one row per NDEF record (`ndef_describe`), then `Write URI` and `Save dump`
    (STARK-0405).
  - Classic, ISO-DEP and read-only tags show `Write URI` disabled with the reason.
  - Module absent or not fitted: a single disabled row `No NFC module`; the app never
    calls `stark_nfc_start`, and logs `nfcapp: no module`.
  - Opening the app starts polling; leaving it stops polling (field off).
* **Write content:** the device has no text entry (a `stark_ui` task, not V0.4), so
  `Write URI` offers a fixed list of lab payloads: `https://example.com/stark`,
  `https://example.org/lab`, and a Text record "STARK ONE lab". This is the project's
  own lab test data, like the IR lab frames.
* **Confirmation (SECURITY_SCOPE §2, ADR-0020 §5):** `"Write NFC tag?"` with
  `"<UID>: URI https://example.com/stark (<n> B of <cap>). Own tags only."`. The same
  text is logged (`nfcapp: confirm …`) before the dialog opens. Only OK writes.
* **Writer** (`nfc_writer.c`, pure, over `pn532_t`):
  - Re-selects the tag and requires the same UID; another or no tag is
    `STARK_ERR_STATE`.
  - Requires a CC with access 0x00 (read-only is `STARK_ERR_NOT_SUPPORTED`).
  - Requires the TLV `03 len msg FE` to fit the CC size (`STARK_ERR_NO_SPACE`).
  - Writes pages from 4 only, within the data area, never lock, OTP or configuration
    pages.
  - Reads back and compares; a mismatch is `STARK_ERR_IO`.
  - Logs `nfc: write ok <pages> pages` / `nfc: write failed: <err>`.
* The app logs `nfcapp: wrote <uid>` / `nfcapp: write refused: <why>` /
  `nfcapp: write failed: <err>`, and shows the tag re-read afterwards.

**Host tests.**
- `test_nfc_writer.c` (production writer + driver + fake):
  - URI and Text written to a blank NTAG215 and read back byte-identical, then parsed;
  - the largest message that fits, and one byte more;
  - a read-only CC;
  - a wrong UID, and no tag;
  - the tag vanishing after each page write (partial write reported, never claimed
    done);
  - a checksum error mid-write recovered;
  - the fake's lock emulation (pages ≥ `ro_from` ignore WRITE) caught by read-back;
  - a proof that no page < 4 or beyond the data area is ever written (the fake records
    every WRITE address).
- `test_ui_nfc.c` (fake `stark_nfc` on the UI port):
  - no-module screen;
  - waiting screen;
  - NTAG with a URI;
  - Classic (write disabled);
  - read-only (write disabled);
  - confirm text exact; cancel writes nothing;
  - write done / failed / refused;
  - goldens inspected.

**Emulator.**
- `v04-nfc-absent.toml` (production image): open NFC → `nfcapp: no module`, no
  `nfc: field on`, BACK → `app: stop nfc`.
- `storage/v04-nfc-app.toml` (test image), driven by pokes:
  - `emu_nfc_tag=1` → `nfc: tag NTAG213 …`, `nfc: ndef …` and the URI shown;
  - `emu_nfc_tag=0` → `nfc: tag left`;
  - `emu_nfc_tag=2` → write `https://example.com/stark` → confirm → `nfc: write ok` →
    re-read shows the URI;
  - `emu_nfc_tag=4` → write refused (read-only);
  - `emu_nfc_tag=3` → `nfc: tag CLASSIC_1K …`, write disabled;
  - `emu_nfc_tag=7` → `nfc: read stopped: Not found`;
  - `emu_nfc_fault` set to bus errors for three polls → `nfc: module lost` → cleared →
    `nfc: module back`;
  - leaving the app → `nfc: field off`.
  - `forbid_between`: no `nfc: write` before `dialog: ok`.

**Fault injection.** Above, on the host and in the test image.

**AC.**
1. The app shows the module, tag, NDEF and absence states; goldens inspected.
2. No write without the confirmation (log order checked); a write is followed by a
   read-back that matches, or the failure is surfaced.
3. Writes never touch pages 0–3, lock, OTP or configuration pages, or anything outside
   the CC data area (host proof); read-only and too-small tags are refused.
4. Module absence: the production image's app shows `No NFC module` and starts no
   polling. Earlier scenarios pass with only `sel=` numbers changed.

**Resource limits.** App static ≤ 3 kB (its copy of the snapshot is ~0.9 kB; the dump
buffer of STARK-0405 ~0.95 kB); app files ≤ 150 lines each; `nfc_writer.c` ≤ 3 kB
flash.

**Out of scope.** Text entry, writing to Classic or ISO-DEP tags, formatting a blank
non-NDEF tag, lock/read-only commands, password protection, copying one tag's NDEF to
another.

**CI.** Host suite, both images. **Security.** Writes are confirmed, limited to the
confirmed UID and the NDEF area, and never lock. **Physical HIL (deferred to V1).**
Writing our own NTAG and reading it back on the device and with a phone; a write
interrupted by pulling the tag away.

---

## STARK-0405 — Dump-to-SD format, V0.4 exit scenarios, soak, measurements, V1 NFC checklist

**Goal.**
- A tag's identification and readable Type 2 memory can be saved to the card in a
  documented, non-proprietary text format, and read back losslessly.
- Every V0.4 exit criterion's software half is demonstrated in CI.
- Every physical half is written down for V1.

**Layer.** L2 pure core (`nfc_dump.c`), app addition, tests, CI, docs.

**Files.**
- `components/stark_nfc/{nfc_dump.c,include/nfc_dump.h,stark_nfc.c,include/stark_nfc.h}` (dump request)
- `apps/app_nfc/nfc_save.c`
- `docs/nfc-format.md` (dump section)
- `test/host/test_nfc_dump.c`
- `test/emu/storage/{v04-reads.toml,v04-write-readback.toml,v04-dump.toml}`,
  `test/emu/soak/v04-soak.toml`
- `.github/workflows/ci.yml` (soak job)
- `docs/bringup/v1-nfc.md` (new), `docs/measurements.md`
- `docs/VALIDATION.md` §3.7, `README.md`, ROADMAP (status), TESTING §4/§5

**Deps.** STARK-0404.

**API.**

```c
/* nfc_dump.h — pure */
#define NFC_DUMP_LINE_MAX 32      /* longest line: "page 230 XXXXXXXX" */
typedef struct { nfc_tag_t tag; uint16_t n_pages; uint8_t mem[NFC_T2_MAX_PAGES * 4]; } nfc_dump_t;
/* Streams the text: writes as many whole lines as fit from *cursor on, advances it,
 * returns the bytes written (0 once done). A full NTAG216 dump is ~4.2 kB, so the app
 * writes it through a small buffer instead of holding it. */
size_t      nfc_dump_format(const nfc_dump_t *d, size_t *cursor, char *out, size_t cap);
stark_err_t nfc_dump_parse(const char *text, size_t len, nfc_dump_t *out); /* strict, bounded */

/* stark_nfc.h — the NFC task reads every readable page of the present tag into *out
 * (the caller's buffer, valid until the event); STARK_SYS_NFC DUMP_DONE / DUMP_FAILED. */
stark_err_t stark_nfc_request_dump(nfc_dump_t *out);
```

**Format (`stark-nfc 1`).** A first line `stark-nfc 1`, then the fields:

```
uid 04A1B2C3D4E5F6
atqa 0044
sak 00
tech NTAG213
pages 45
page 0 04A1B2C3
...
```

* `#` lines are comments; field order is fixed; the parser is strict and bounded, like
  `ir_raw.c`.
* Only pages the tag returned are present; Classic and ISO-DEP dumps carry no pages.
* Saved as `captures/nfc_NNNN.nfc` (next free number) on the app worker, atomically.
* There is no import back to a tag: a dump is for inspection only (ADR-0020 §5).

**Notes (close-out, as STARK-0307).**
* `storage/v04-reads.toml`: 20 arrive/leave cycles of each Type 2 catalogue tag →
  20/20 `nfc: tag` lines with identical fields per tag (exit 1, software half).
* `storage/v04-write-readback.toml`: write → read back → the same URI (exit 2).
* `storage/v04-dump.toml`: save a dump → `nfcapp: saved nfc_0001.nfc`.
* `v04-nfc-absent.toml` (from STARK-0404) carries exit 3.
* `soak/v04-soak.toml` (test image), 60 s:
  - NFC app polling, 10 arrivals and removals, 2 writes, 2 dumps, faults recovered;
  - an IR replay and a write test;
  - then idle at the root;
  - gates: `drops=0`, heap Δ ≤ 1 kB.
* `docs/bringup/v1-nfc.md`: every ROADMAP V1 exit 10 item with expected values and
  blank results.
* `docs/measurements.md`: V0.4 rows and the exit table (physical: deferred to V1).
* README status → "V0.4 complete (software/protocol-verified; real tags and RF at V1)".

**Host tests.** `test_nfc_dump.c`:
- format → parse round trip for every catalogue tag;
- NDEF storage round trip: dump → parse → pages → TLV → `ndef_parse` gives the same
  records;
- malformed dumps: every line truncated, bad hex, page out of range, wrong version,
  reordered fields;
- 10 000 fuzzed inputs.

**Emulator.** The scenarios above, and the soak in the CI soak job.

**Fault injection.** Card absent or full during a dump save (host UI port and the
storage image's medium switch).

**AC.**
1. Every V0.4 exit criterion's software half is met with evidence; every physical half
   is listed and none claimed; `docs/bringup/v1-nfc.md` exists with blank results.
2. All V0.4 scenarios and the four soaks pass in CI; `drops=0`.
3. `nfc_dump.c` has 100 % line coverage; the simulator-name grep of STARK-0402 AC3 is
   still empty.
4. No document describes fake or emulator results as a real tag read (grep for "tag
   read", "real tag" and "RF" reviewed).

**Resource limits.** One `nfc_dump_t` (~0.95 kB) in the app, filled by the NFC task and
written by the app worker through a 256-byte line buffer; `nfc_dump.c` ≤ 3 kB flash.

**Out of scope.** Dump import or write-back, dumps of protected memory, Classic sector
dumps, a dump browser (files are read on a PC).

**CI.** The soak job runs the V0.4 soak too. **Security.** Dumps hold only what our own
tag returns to an unauthenticated read; no keys, no restore. **Physical HIL (deferred to
V1).** `docs/bringup/v1-nfc.md` in full.

---

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
