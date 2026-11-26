# V1 bring-up — core platform (power, boot, display, input, storage)

The physical half of the platform itself: ROADMAP V1 exits 2–8 (the performance gates
deferred by ADR-0011 and the V0.2 storage gates deferred by ADR-0018), and the abbreviated
breadboard checklist of TESTING.md §5, written out as numbered checks. Written by
STARK-0804. **Nothing here has been done.**

Every simulated figure for these gates (boot time, `display: full refresh`, FPS) was logged
but never gated (TESTING §1.1): the emulator has no panel, no SPI device timing and no
switch, and the storage gates were met in software only (docs/measurements.md "V0.2"). A row
is PASS only with the measurement or observation written in it. Do this procedure first: the
module, IR, NFC, Sub-GHz and lab procedures assume a board that has passed it.

**Scope (SECURITY_SCOPE §1).** Our own board, parts and cards. 3.3 V logic only
([v1-parts.md](v1-parts.md)).

## Setup

| Item | Value | Source |
| --- | --- | --- |
| Board | ESP32-S3-DevKitC-1-N16R8; revision (v1.0 / v1.1): `________` | HARDWARE.md §1, §2.2 |
| Wiring | HARDWARE §2.1 and §2.2 pin map, unchanged; SPI jumpers under 10 cm | HARDWARE.md §2.3 |
| Parts | [v1-parts.md](v1-parts.md) §1.1 (core); card breakout per §3.3 | ADR-0024 |
| Firmware | production image, the main SHA under test: `________` | — |
| Breadboard build settings | `CONFIG_STARK_DISPLAY_SPI_HZ=20000000` (the default is the PCB's 40 MHz; HARDWARE §2.3), set with `idf.py menuconfig` → STARK, and written here with any other non-default value: `________` | `components/stark_display/Kconfig.stark` |
| Panel settings | `STARK_DISPLAY_SWAP_XY` y, `MIRROR_X` n, `MIRROR_Y` n, `BGR` y, `INVERT` n (defaults); values that were changed to pass checks 7–8: `________` | `components/stark_display/Kconfig.stark` |
| Card clock | `STARK_STORAGE_SD_KHZ` 20000 (init at 400 kHz) | `components/stark_storage_medium/Kconfig.stark` |
| Console | USB, 115200 Bd (ADR-0015) | — |
| Instruments | multimeter, inline USB power meter or current probe, logic analyser (≥ 8 channels, ≥ 24 MS/s is enough here), oscilloscope for bounce and MISO release | v1-parts.md §2 |

Log lines used below, as the firmware prints them: `boot: ui_ready in <n> ms`
(`main/stark_main.c`), `display: init …`, `display: full refresh <n> ms`
(`components/stark_display`), `diag: heap=… min=… fps=… drops=… overruns=…` (every
`STARK_DIAG_LOG_PERIOD_S`, default 10 s), `menu: sel=<n> "<label>"`, `displaytest: fps=…`,
`buzzertest: tone <n> Hz`, `settings: …`, `storage: mounted …` / `no card` /
`unusable card: …` / `card removed`, `storageapp: wtest start` / `wtest ok bytes=… crc=…
frames=…` / `wtest failed: <err> at <offset>`.

## Checks

Do them in order: a failing check in a group stops that group (HARDWARE §8 bring-up order).

### Power and boot

| # | Check | How | Pass criterion | Result | Evidence |
| --- | --- | --- | --- | --- | --- |
| 1 | 3V3 rail | Multimeter at the DevKit 3V3 pin: idle at the root menu, backlight at 100 %, then during check 12's full refresh | 3.20–3.40 V (±3 %) in every state | | |
| 2 | Idle current | USB power meter, root menu, backlight 100 % and 50 % | Recorded (HARDWARE §3.1 estimates 120 mA idle with backlight) | | |
| 3 | Boot and console | Reset; console at 115200 | `boot: stark-one <ver> idf=… heap=…`, `boot: build: … chip: esp32s3 rev: …`, `display: init 320x240 …`, `boot: ui_ready in <n> ms`; no `E (` line, no reset loop | | |
| 4 | `boot: ui_ready` ≤ 600 ms (V1 exit 2) | Ten resets; the logged figure, and on the analyser from the rising edge of `EN` (the reset button) to the first `TFT_CS` low after `RAMWR` of the root menu | Every run ≤ 600 ms; worst, median and the log-vs-analyser difference recorded | | |
| 5 | First pixel ≤ 400 ms (V1 exit 2) | Same ten resets; analyser from `EN` rising to the first `TFT_DC`-high data byte after the first `RAMWR` (0x2C) | Every run ≤ 400 ms | | |

### Display and SPI

| # | Check | How | Pass criterion | Result | Evidence |
| --- | --- | --- | --- | --- | --- |
| 6 | Picture present | Root menu | Text readable, backlight on, no snow or noise | | |
| 7 | Orientation and mirroring | Display Test, the text sample and the 1 px grid | Upright landscape 320×240, text reads left to right, not transposed; if wrong, change only the SWAP/MIRROR settings and record them | | |
| 8 | Colour order | Display Test colour bars | The red bar is red (if blue, `STARK_DISPLAY_BGR` is wrong for this panel; fix it there, never in `stark_gfx`) | | |
| 9 | Band seams | Display Test gradient and grid, a photo | No offset, gap or tear at any band boundary (`band=` from the init line) | | |
| 10 | SPI at 20 MHz | Analyser on `TFT_SCLK` during a refresh; 10 minutes in Display Test | 20 MHz ± 1 %; no corruption over the 10 minutes | | |
| 11 | SPI at 40 MHz | Rebuild with `STARK_DISPLAY_SPI_HZ` 40000000; repeat check 10 | Recorded: stable or not; the breadboard keeps 20 MHz if not (a 40 MHz pass is a PCB gate) | | |
| 12 | Full-screen refresh ≤ 60 ms (V1 exit 3) | `display: full refresh <n> ms` at the chosen clock; analyser across the first and last band's `TFT_CS` of one refresh | ≤ 60 ms by both; both figures recorded | | |
| 13 | Backlight PWM | Settings → brightness 10 %, 50 %, 100 %; analyser on `TFT_BL` | Duty follows the setting; no visible flicker; current at each step recorded | | |

### Input

| # | Check | How | Pass criterion | Result | Evidence |
| --- | --- | --- | --- | --- | --- |
| 14 | Six keys | Input Test; each key 20 times | Each press is one press of the right key; no key reads as another | | |
| 15 | Bounce | Scope on each key line at press and release; then 100 fast taps on DOWN in the root menu | Bounce duration recorded per switch (the firmware needs < 20 ms, `INPUT_DEBOUNCE_MS`); exactly 100 `menu: sel=` moves, no doubles | | |
| 16 | Long press and repeat | Input Test: hold OK 1 s, hold DOWN 2 s; analyser on the key line, console timestamps | OK: one LONG at 500 ms ± 20 ms, no repeat; DOWN: first REPEAT at 400 ms, then every 120 ms ± 10 ms | | |
| 17 | Key-to-pixel ≤ 50 ms (V1 exit 4) | Root menu; analyser on `BTN_DOWN` and `TFT_CS`; 20 single DOWN presses | From the press edge to the end of the redraw's last `TFT_CS` transaction ≤ 50 ms every time; worst and median recorded | | |

### Frame rate and overruns

| # | Check | How | Pass criterion | Result | Evidence |
| --- | --- | --- | --- | --- | --- |
| 18 | Root menu ≥ 20 FPS (V1 exit 3) | Hold DOWN in the root menu for 30 s (repeat scrolling); the `diag:` lines; then Display Test's `displaytest: fps=` | `diag` fps ≥ 20 while scrolling; Display Test's figure recorded as the panel's ceiling at the chosen clock | | |
| 19 | No frame overruns over 60 s (V1 exit 5) | 60 s of navigation through every app's first screen; `diag:` lines | `overruns=` unchanged and `drops=0` over the 60 s | | |

### Buzzer and LED

| # | Check | How | Pass criterion | Result | Evidence |
| --- | --- | --- | --- | --- | --- |
| 20 | Buzzer | Buzzer Test: every tone, then the melody; volume 100 % in Settings; scope on 3V3 | Audible at each `buzzertest: tone <n> Hz`; key clicks audible; 3V3 dip at full volume recorded, no brown-out reset | | |
| 21 | Status LED | Watch for 10 s; analyser on GPIO 18 | Heartbeat at 1 Hz (`stark_led.c`, 100 ms ticks) | | |
| 22 | DevKit RGB LED | Note the board revision's RGB LED (GPIO 48 on v1.0, 38 on v1.1) during the IR and lab procedures | Recorded as cosmetic flicker only (HARDWARE §2.2) | | |

### Storage (V1 exit 8, ADR-0018)

| # | Check | How | Pass criterion | Result | Evidence |
| --- | --- | --- | --- | --- | --- |
| 23 | Settings survive a power cycle | Change brightness, volume and repeat in Settings; unplug USB for 10 s; boot | `settings: loaded brightness=… volume=… repeat=…` shows the changed values | | |
| 24 | Card mounts | A FAT32 card of ours; boot | `storage: mounted sdspi total=… free=…`; `storage: logging to logs/stark.log` | | |
| 25 | 100 kB read back byte-identical while rendering | Storage → Write test (100 kB), pressing DOWN during it; analyser on `TFT_CS`, `SD_CS`, `SPI_SCLK` | `storageapp: wtest ok bytes=102400 crc=… frames=…` with frames > 0; the key press handled during the write | | |
| 26 | Worst band-flush stall during SD commands | The capture of check 25 | Longest gap between two band flushes while `SD_CS` is active, recorded (it feeds the PCB's clock and band decisions) | | |
| 27 | Card pulled mid-write | Start the write test; pull the card at about half way | `storageapp: wtest failed: <err> at <offset>` and `storage: card removed` shown and logged; no panic, no reset; UI responsive | | |
| 28 | Reinsertion remounts | Reinsert the card from check 27; Storage → Mount if not automatic | `storage: mounted …` again; a new write test passes | | |
| 29 | No card, non-FAT card, faulty card | Boot with: no card; a card formatted non-FAT; a card that fails (for example one that fails initialisation in a PC) | `storage: no card` / `storage: unusable card: …` respectively; every app other than storage works | | |
| 30 | Panel and card share SPI2 | Capture of check 25 | Panel and card transactions never interleave inside a transaction; `SD_CS` high whenever the panel is addressed | | |
| 31 | MISO released by the card breakout | Scope on `SPI_MISO` with `SD_CS` high, during panel traffic and at idle | MISO high-impedance (pulled level, not driven) whenever `SD_CS` is high (ADR-0024 §3) | | |

### Endurance and records

| # | Check | How | Pass criterion | Result | Evidence |
| --- | --- | --- | --- | --- | --- |
| 32 | Thermals | 15 minutes of Display Test; thermometer or IR thermometer on the module can | Temperature recorded (ambient too) | | |
| 33 | One hour, no leak | One hour at the root menu with a key press every few minutes; first and last `diag:` line | `heap=` within 1 kB of the first line, `drops=0`, no reset | | |
| 34 | Figures recorded (V1 exit 6) | docs/measurements.md "Hardware" section; the logged figures from this procedure's captures with `scripts/bringup_figures.py LOG... --compare EMU_LOG...` (STARK-0807), the analyser figures from checks 4, 5 and 17 | Boot time, first pixel, refresh, FPS, key latency, free heap, binary size and current recorded next to the simulated figures | | |
| 35 | Pin map unchanged (V1 exit 7) | Compare the wiring with HARDWARE §2 and `stark_board` | Unchanged, or every change made in `stark_board` and `diagram.json` together in a task | | |

## After the procedure

A FAIL becomes a task in TASKS.md; the firmware's limits are not changed to make a row pass.
Once every row is PASS, ROADMAP V1 exits 2–8 can be marked against this document.
