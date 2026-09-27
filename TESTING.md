# TESTING & QUALITY GATES — STARK ONE

Testing is structured so that the **fastest loop catches the most bugs**: pure logic on
the host in milliseconds, the production firmware in Espressif's free emulator in
seconds, hardware reality on a checklist. Every mandatory tier is free and local
(ADR-0017); the tier model, emulator limits and the coverage matrix are in
[docs/VALIDATION.md](docs/VALIDATION.md).

---

## 1. Test levels

| Level | What it covers | Where it runs | Speed | Gate |
| --- | --- | --- | --- | --- |
| L1 Host unit | Pure cores: event bus, gfx primitives/text, input FSM, menu model, protocol codecs, parsers | Host (macOS/Linux), CMake + Unity | < 2 s | Every commit |
| L2 Build | Compilation of the whole firmware, warnings-as-errors, layer rules | Docker `espressif/idf:v6.1` | ~2 min | Every commit |
| L3 Emulator | The production image: boot, FreeRTOS, timers, input→event→UI→app flow, logs, heap, panics | esp-emulator (pinned), local + CI | ~15–50 s/scenario | Every commit |
| (optional) Wokwi | Extra confidence, e.g. a simulated ILI9341 | Wokwi, free quota only | ~20 s/scenario | Never required (ADR-0017) |
| L4 Hardware | Panel config, SPI integrity, bounce, SD reliability, RF/IR/NFC behaviour, **all performance gates**, power | Bench, checklist | manual | Per hardware milestone |

Nothing is tested twice at two levels without reason: if a behaviour is host-testable,
it does not get an emulator scenario.

## 1.1 What each level is allowed to assert

This split is normative (ADR-0011) and exists because the most common way to get a
false green is to gate on a number the environment cannot produce honestly.

| Property | Host | Emulator | Hardware |
| --- | --- | --- | --- |
| Logic correctness, ordering, state transitions | ✅ gate | ✅ gate | ✅ |
| Output values, log lines | ✅ gate | ✅ gate | ✅ |
| External pin levels (LED, buzzer, CS) | ✅ gate on a test port | ❌ not observable (docs/VALIDATION.md §2) | ✅ gate |
| Memory: free heap, leak deltas, buffer sizes | ✅ gate | ✅ gate | ✅ gate |
| Counters: dropped events, error counts, retries | ✅ gate | ✅ gate | ✅ gate |
| Rendering correctness (clipping, seams, regions, screen pixels) | ✅ gate | ❌ no panel model | ✅ glass |
| **Boot time (ms), FPS, refresh duration, latency, throughput** | ❌ | 📊 **log only, never a gate** | ✅ **gate** |
| RF/IR/NFC physical behaviour | ❌ | ❌ | ✅ gate |

Optional Wokwi runs may assert the same functional properties as the emulator (and pin
levels, screenshots); they never gate and never stand alone as evidence.

📊 = recorded in `docs/measurements.md` as informational trend data. A simulated
figure moving is worth a look; it is never a build failure.

---

## 2. L1 — host unit tests

**Project:** `test/host/` is a standalone CMake project (not an ESP-IDF project) that
compiles the pure core sources directly:

```
test/host/
├── CMakeLists.txt          # C11, -Wall -Wextra -Werror, sanitizers on by default
├── unity/                  # pinned Unity, vendored or FetchContent with a fixed tag
├── support/
│   ├── stark_hal_host.c    # fake clock + fake GPIO array
│   └── ppm_dump.c          # writes a surface to .ppm for eyeball inspection
└── test_event.c  test_gfx.c  test_text.c  test_input_core.c  test_menu_model.c
```

**Rules**

1. Host tests must compile **without ESP-IDF on the include path**. This is the
   mechanical enforcement of "cores are pure" (ARCHITECTURE §10).
2. No test sleeps. Time is injected: `hal_host_set_now_us()` advances the fake clock.
3. Tests are deterministic and order-independent; no globals leak between cases.
4. Build with `-fsanitize=address,undefined` locally and in CI.
5. Graphics tests assert on pixel values in a memory surface, plus a `.ppm` artifact on
   failure so a human can look at what went wrong.

**Coverage target:** ≥ 80 % lines over `stark_event`, `stark_gfx`, `stark_input_core`,
`stark_ui_model` by the end of V0; ≥ 90 % over any protocol codec (IR/NFC/Sub-GHz) at
its milestone. Coverage is measured with `--coverage` + `gcovr` and reported, not
enforced as a hard gate below the target (a hard gate on a young codebase produces
test-shaped noise).

**Representative cases to write**

* *Event bus:* publish/dispatch ordering; capacity overflow drops the oldest and counts
  it; type-mask filtering; subscribe during dispatch; empty dispatch is a no-op.
* *Input FSM:* bounce shorter than 20 ms is ignored; press→release < 500 ms = SHORT;
  hold ≥ 500 ms = exactly one LONG; repeats start at 400 ms and recur every 120 ms;
  two keys tracked independently; OK/BACK never repeat.
* *gfx:* clipping at all four edges; zero/negative sizes; `origin_x/y` translation puts
  pixels in the right band; fill/rect/hline/vline boundary pixels; blit with and
  without transparency.
* *text:* width matches rendered extent; UTF-8 multi-byte decode; unmapped code point
  renders the fallback glyph; clipping mid-glyph.
* *menu model:* selection wrap; scroll window follows selection; paging at list
  boundaries; empty list; list shorter than the window; disabled items are skipped.

---

## 3. L2 — build & static quality gates

Run in CI on every commit, and locally via `scripts/check.sh`:

| Gate | Tool | Policy |
| --- | --- | --- |
| Clean build | `idf.py build` in `espressif/idf:v6.1` | Zero warnings. ESP-IDF v6 treats warnings as errors by default; we additionally enable `-Wshadow -Wconversion -Wdouble-promotion` on our own components only — everything under `components/`, `apps/`, `main/`, never a vendor component (root `CMakeLists.txt`, STARK-0100). `-Wundef` runs in the host build over every pure core but not in the firmware build: ESP-IDF's public headers test unset `CONFIG_` symbols with `#if`, and enabling it produced 1 026 diagnostics, all inside vendor headers (e.g. `esp_common/include/esp_compiler.h:19: 'CONFIG_COMPILER_OPTIMIZATION_PERF' is not defined`), none in our sources (STARK-0100 evidence) |
| Formatting | `clang-format` (config committed) | `scripts/fmt.sh --check` must produce no diff |
| Layer rules | `scripts/check_layers.py` | Component `REQUIRES` must respect the layer table and allowlist in ARCHITECTURE §10 (STARK-0100) |
| Pin-map consistency | `scripts/check_pins.py` | `diagram.json` connections must match `stark_board` pins |
| Static analysis | `clang-tidy` (bugprone-*, cert-*, readability-* subset) over `stark_*` sources | Findings fail CI; suppressions require an inline reason comment — **not yet active** (TASKS.md infrastructure debt register) |
| Secrets | `gitleaks` | Any hit fails; no exceptions |
| Binary size | `idf.py size` recorded per build | Recorded per build; the ±10 % regression flag in the PR is **not yet active** (TASKS.md infrastructure debt register) — sizes are tracked in `docs/measurements.md` |
| Dependency lock | `dependencies.lock` committed | CI fails if `idf.py build` modifies it |

**Warnings policy, explicitly:** a warning is a bug we have not read yet. No
`#pragma GCC diagnostic ignored` without a comment naming the vendor issue it works
around. No `-Wno-*` added to our own components.

---

## 4. L3 — emulator scenarios (and optional Wokwi)

Mandatory scenarios live in `test/emu/*.toml` and run the production merged image in
the pinned esp-emulator (`scripts/test_emu.sh`; harness `scripts/test_emu.py`, format
in its docstring and [docs/VALIDATION.md §2](docs/VALIDATION.md)). They assert on:

* **serial output** — the primary surface, in order. UI and app state transitions log
  terse, stable lines at INFO level specifically so CI can assert on them;
* **heap** — the first `diag: heap=` against a scenario's `heap_min`;
* **forbidden output** — `panic:`, Guru Meditation, `ui: render failed`, aborts.

Key presses enter at the input core's sampling boundary (GDB, emulated milliseconds);
pixels and pin levels are not observable in the emulator and are covered on the host
or deferred to HIL. The Wokwi YAML scenarios in `test/scenarios/` remain for optional
runs ([WOKWI.md §6](WOKWI.md)).

**Timing rule:** a scenario may assert that `boot: ui_ready in 412 ms` *appeared*, and
that it appeared after `display: init` and before the first `menu:` line. It may not
assert anything about `412`. The same applies to FPS, refresh durations and any other
wall-clock figure (§1.1).

**Contract:** log lines asserted by a scenario are part of that task's public contract.
Changing the wording is a breaking change and must update the scenario in the same
commit. Reserved prefixes:

| Prefix | Emitted by | Example |
| --- | --- | --- |
| `boot:` | `stark_main` | `boot: ui_ready in 412 ms` |
| `menu:` | `stark_ui` | `menu: sel=2 "Input Test"` |
| `app:` | `stark_app` | `app: start inputtest` / `app: stop inputtest` |
| `key:` | `stark_input` (DEBUG) | `key: OK short` |
| `diag:` | `stark_diag` | `diag: heap=228412 min=224000 fps=30.0 drops=0 overruns=0` (STARK-0109; `heap=` stays first) |
| `dialog:` | `stark_ui` dialogs | `dialog: open "Confirm"` / `dialog: ok` / `dialog: cancel` (STARK-0105) |

**Scenario budget:** ≤ 20 s of emulated time each. Emulator scenarios are free and
all run on every push and pull request. (Wokwi's quota once forced a push-only smoke
subset; with Wokwi optional that constraint is gone.) The one exception is the V0.1 soak (60 s simulated, ROADMAP
V0.1 exit 3): it lives in `test/scenarios/soak/`, outside the default set, and runs in a
`workflow_dispatch`-only CI job so it costs minutes only when a milestone close-out asks
for it.

**Planned scenarios by milestone**

Emulator scenarios are `test/emu/<name>.toml`; the V0 Wokwi YAML files remain for
optional runs. Planned emulator capabilities for later milestones are confirmed at
that milestone before a scenario depends on them.

| Milestone | Scenario | Asserts |
| --- | --- | --- |
| V0 | `v0-boot.toml` | Boot lines appear in order, `boot: ui_ready` present, `diag: heap=` above the threshold, first full refresh completes |
| V0 | `v0-boot-and-menu.toml` | Navigate, launch an app, BACK returns to menu |
| V0 | `v0-input-matrix.toml` | All six keys produce the expected `key:` lines |
| V0.1 | `v01-dialog.toml` | Modal confirm opens, OK/BACK behave, a key pressed before opening cannot confirm (overruns are reported, never asserted — ADR-0011) |
| V0.1 | `v01-navigation.toml` | Long-BACK unwinds dialog + app to the root; OK+BACK chord is reserved |
| V0.1 | `v01-app-lifecycle.toml` | Worker joined before `on_stop`; faults unwind to the launcher with an alert |
| V0.1 | `v01-diag.toml` | Diagnostics logs live heap and FPS (the screen's pixels: host render check) |
| V0.1 | `soak/v01-soak.toml` | 60 s of emulated use: `drops=0` throughout, root heap delta ≤ 1 kB |
| V0.2 | `v02-settings-persist.toml` | Change a setting, reset via the emulator control channel, value survives (NVS in emulated flash) |
| V0.2 | (host test port + HIL) | 100 kB SD write/read-back while the UI renders — esp-emu models no SD card on GP-SPI |
| V0.7 | `v07-i2c-scan.toml` | Scanner finds esp-emu's built-in I²C EEPROM (and nothing else) |
| V0.7 | `v07-uart-loopback.toml` | UART bridge round-trips via esp-emu's `--uart1-tcp` side channel |

---

## 5. L4 — hardware verification

A written checklist per hardware milestone, stored at `docs/bringup/<milestone>.md`,
filled in and committed with measurements — not ticked from memory.

**Performance gates live here.** Everything the simulated milestones logged but did not
gate is verified on hardware at V1 prototype, against the targets in
[ROADMAP.md](ROADMAP.md): `boot: ui_ready` ≤ 600 ms, first pixel ≤ 400 ms, ≥ 20 FPS at
the root menu, full-screen refresh ≤ 60 ms, key-to-pixel latency ≤ 50 ms, no frame
overruns over 60 s. Each figure is measured, recorded in `docs/measurements.md`
alongside its simulated counterpart, and is a pass/fail condition for the milestone.

**Breadboard bring-up checklist (V1 prototype), abbreviated**

1. Power: 3V3 rail within ±3 %, current at idle recorded.
2. Boot: device enumerates, console at 115200, `boot:` lines appear; boot time measured.
3. Display: correct orientation, no mirroring, colours correct (red is red — if not,
   the BGR flag is wrong), no tearing at the band boundaries, full-screen refresh timed
   and compared against the ≤ 60 ms gate.
4. SPI integrity: works at 20 MHz; attempt 40 MHz and record whether it is stable.
5. Buttons: all six, no double-triggering, repeat timing feels right, bounce measured
   on a scope if anything is suspicious.
6. Buzzer: audible, tone range sane, no MCU brownout at max volume.
7. LED: heartbeat visible.
8. Thermals: module temperature after 15 minutes of use.
9. Longevity: 1 hour of continuous operation, then `diag:` heap unchanged (leak check).

Radio milestones add their own: IR range and protocol fidelity against our own remotes;
NFC read reliability over 20 attempts per tag; Sub-GHz RSSI versus distance, and a
verified duty-cycle limiter.

---

## 6. Reproducible development environment

| Aspect | Mechanism |
| --- | --- |
| Toolchain version | `.idf-version` file + `scripts/setup.sh` assertion + `espressif/idf:v6.1` in CI |
| Registry components | Exact versions in `idf_component.yml` for load-bearing components (`espressif/esp_lcd_ili9341` is pinned `==2.1.0`, fallback `==2.0.2` — ADR-0004); `dependencies.lock` committed; CI fails if the build changes it |
| Host test deps | Unity pinned by tag (vendored or FetchContent with an exact tag) |
| Build config | `sdkconfig.defaults` + `sdkconfig.defaults.esp32s3` committed; `sdkconfig` itself is **git-ignored** |
| Container | `docker run --rm -v $PWD:/p -w /p espressif/idf:v6.1 idf.py build` reproduces CI exactly |
| Wokwi | `diagram.json` + `wokwi.toml` committed; `wokwi-cli` version pinned in CI |
| Scripts | Everything CI does is a script in `scripts/` that a human can run identically |

**Secrets policy.** No tokens, keys, credentials, Wi-Fi passwords or capture files
containing real identifiers in the repository — ever. `WOKWI_CLI_TOKEN` lives in CI
secrets. Local overrides live in `local.env` / `sdkconfig.local`, both git-ignored.
`gitleaks` runs in CI. Any capture committed as a test vector must come from our own
hardware and must be documented as such in the test file.

---

## 7. CI pipeline

`.github/workflows/ci.yml`, as it runs today (items marked *planned* are owned by the
named task or the TASKS.md infrastructure debt register):

```
lint      (ubuntu-latest, ~30 s)
  ├─ clang-format --check        (scripts/fmt.sh --check)
  ├─ check_layers.py
  ├─ check_pins.py
  ├─ wokwi_gate.py unit tests
  └─ gitleaks

host      (ubuntu-latest + macos-latest, ~1 min)
  └─ scripts/test_host.sh        (sanitised Unity suite)

firmware  (ubuntu-latest, espressif/idf:v6.1, ~2-3 min)
  ├─ idf.py build                (warnings-as-errors)
  ├─ idf.py size                 (log; PR comment + ±10 % flag planned: debt register)
  ├─ verify dependencies.lock unchanged
  └─ upload firmware artifact for the wokwi job

esp-emulator (needs firmware)
  ├─ scripts/install_esp_emu.sh  (pinned esp-emu, SHA-256 verified)
  └─ scripts/test_emu.sh         (every test/emu/*.toml on the production image)

wokwi.yml — OPTIONAL, never required (ADR-0017): manual dispatch or `wokwi` PR label
  ├─ scripts/wokwi_gate.py       (runtime-relevant change set? else "not applicable")
  └─ scripts/test_wokwi.sh

clang-tidy on stark_*            (planned: debt register)
```

Merge rule: the mandatory, free jobs — `lint`, `firmware`, `host` (both OSes) and
`esp-emulator` — must be green before merge. The optional Wokwi workflow never gates a
merge; a red optional run is reported, not hidden. `main` has no GitHub branch protection configured; the rule is
enforced by the working agreement (AGENTS.md), not by the repository settings.
`main` is always releasable — if a milestone is mid-flight, it lives on a branch.

---

## 8. What we deliberately do not test

| Not tested | Why |
| --- | --- |
| Vendor components (`esp_lcd`, FATFS, NVS) | Espressif's job; we test our usage, not their code |
| Exact pixel output of every screen | Brittle; we test primitives exactly and screens by smoke/eyeball |
| Simulated timing / performance | Wokwi's timing is not the device's timing; every wall-clock gate is a hardware measurement (§1.1, ADR-0011) |
| Error paths of `malloc` at init | Init failure panics deliberately; testing every OOM branch is not worth the harness |
| UI aesthetics | Human review, with Wokwi screenshots in the PR |
