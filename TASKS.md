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

**Files.** `test/scenarios/soak/v01-soak.yaml` (new), `scripts/test_wokwi.sh`,
`.github/workflows/ci.yml` (`wokwi-soak` job), `docs/measurements.md`, `README.md`,
`WOKWI.md` §6, TESTING.md §4.

**Deps.** STARK-0109.

**Notes.**
* `v01-soak.yaml`: 60 s of simulated use cycling through every app (launch, interact,
  BACK), a confirm and an alert, a worker demo, a contained fault and a long-BACK home,
  ending at the root. It lives in `test/scenarios/soak/`, outside the default set, and
  declares `# stark-timeout-ms: 90000`; the runner honours that header (default 30 000)
  and accepts `--soak` to run the soak directory.
* Runner soak checks: `drops=0` on every `diag:` line (already), and the first and last
  root-menu `diag: heap=` differ by ≤ 1 024 B.
* CI: a `wokwi-soak` job on `workflow_dispatch` only (Wokwi minutes); the close-out PR
  triggers it once and links the run. TESTING §4 records the soak as the one budget
  exception (60 s simulated) and why it is off the per-PR path.
* `docs/measurements.md` gains V0.1 rows (Simulated, informational unless gated):
  heap at root, soak heap delta, drops, overruns *reported*, FPS, binary size and the
  flash cost of the fonts; plus a V0.1 exit-criteria evidence table. Hardware table
  unchanged.
* README status → "V0.1 complete".

**Host tests.** Whole suite green; coverage report attached (pure cores and V0.1
pure helpers).

**Wokwi/runtime.** Full pull-request set green; one green `wokwi-soak` dispatch run.

**AC.**
1. Every V0.1 exit criterion (ROADMAP) demonstrably met with evidence in
   `docs/measurements.md`.
2. `v01-soak.yaml` passes in a CI dispatch run: `drops=0` throughout, heap delta ≤ 1 kB.
3. The hello app still satisfies exit 1 (`wc -l`, `grep` from STARK-0104).
4. `grep -ri wokwi components/ apps/ main/` returns nothing.
5. No scenario asserts a wall-clock value; overruns appear only as reported figures.

**Out of scope.** V0.2 work, hardware measurements.

**CI.** New dispatch-only job. **Hardware.** None; V0.2 is also simulation-first (Wokwi
microSD part) — the first milestone needing hardware halves is V0.3.
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
