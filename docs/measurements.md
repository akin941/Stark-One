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

### V0.2 (esp-emulator 0.44.0; production image and storage test image)

Same environment as V0.1. The **storage test image** (ADR-0018) is the production
firmware with only `stark_storage_medium` replaced by a wear-levelled FAT volume on an
internal-flash partition; its figures describe software logic, never a microSD card.

| Date | Task | Metric | Value | Source |
| --- | --- | --- | --- | --- |
| 2026-10-24 | STARK-0207 | Free internal heap at boot, production image — **gate ≥ 200 kB** | 288 832 B (V0.1: 308 176 B) | every production scenario |
| 2026-10-24 | STARK-0207 | Boot heap change per task, measured at each merge: 0201 storage task −5 072 B; 0202 Storage app −468 B; 0203 file slots + 4 kB write buffer −4 880 B; 0204 NVS + settings −3 544 B; 0205 Settings app −196 B; 0206 log ring 4 kB + 1 kB chunk −5 184 B (total −19 344 B) | — | `diag: heap=` at boot, per PR |
| 2026-10-24 | STARK-0201 | Heap cost of a mounted volume (storage image, flash-FAT medium — the SD figure is V1) | −9 824 B | storage image root heap |
| 2026-10-24 | STARK-0207 | Root heap, first vs last stark_diag line over the 60 s V0.2 soak (3 write tests, eject/mount, settings, pull/insert) — **gate ≤ 1 kB** | 285 468 → 285 512 B (Δ 44 B) | `soak/v02-soak` (storage image) |
| 2026-10-24 | STARK-0207 | Root heap over the V0.1 soak on the V0.2 production image — **gate ≤ 1 kB** | 292 072 → 292 108 B (Δ 36 B) | `soak/v01-soak` |
| 2026-10-24 | STARK-0202 | Root heap after 10 remove/insert cycles — **gate ± 256 B** | 296 928 → 296 812 B (Δ 116 B) | throwaway scenario, storage image |
| 2026-10-24 | STARK-0207 | Event drops over both soaks and every scenario — **gate 0** | 0 | `test_emu.py` drops gate |
| 2026-10-24 | STARK-0203 | 100 kB write + read-back (storage image, informational): UI frames rendered during the test | 17 (CRC-32 `f9541b81`, byte-identical) | `storage/v02-files`, `v02-write-under-render` |
| 2026-10-24 | STARK-0206 | Task stack never touched (fill-pattern dump, storage image): esp_timer task / storage task | 2 500 of 3 584 B / 2 024 of 4 096 B | GDB dump |
| 2026-10-24 | STARK-0207 | Application binary (`stark-one.bin`) | 376 576 B (V0.1: 272 000 B; FATFS + SDSPI +76 kB, NVS +24 kB) | `idf.py build` |
| 2026-10-24 | STARK-0207 | Host line coverage, pure sources | 1 282 / 1 286 (99.7 %); every V0.2 pure file 100 % (storage core/path/file/log, log ring/rotate, settings core, buzzer volume, input core) | `test_host.sh --coverage` |

### V0.2 exit criteria — evidence (ROADMAP.md "V0.2", ADR-0018)

V0.2 completion is **software logic verified**. Every physical half is open until the V1
prototype (ROADMAP V1 exit 8) and is **not claimed** here.

| # | Criterion | Software evidence (V0.2) | Physical evidence |
| --- | --- | --- | --- |
| 1 | Settings survive reboot | `v02-settings-store.toml` (defaults → reboot → NVS → `erase-region` → reboot → defaults) and `v02-settings-persist.toml` (set in the app → BACK commits → reboot → loaded and applied; restore defaults → reboot), production image; `test_settings_core.c` (every fake-NVS failure) | **Deferred to V1:** survival across a real power cycle |
| 2 | 100 kB written and read back byte-identical while the UI renders | `storage/v02-write-under-render.toml` (a key press handled during the write; byte compare + CRC-32; frames rendered meanwhile) and `storage/v02-files.toml`; `test_storage_file.c` | **Deferred to V1:** real microSD on the shared SPI2 bus, worst band-flush stall measured |
| 3 | Card removed mid-write degrades gracefully | `storage/v02-remove-mid-write.toml` (sector I/O fails 20 480 B in → `wtest failed: I/O error`, `storage: card removed`, alert, no panic; reinsertion remounts, temp swept, next write passes); `test_storage_file.c` (EIO at every step, ENOSPC, short writes) | **Deferred to V1:** real pull, detection latency, reinsertion |
| 4 | Mount failure leaves the device usable | `v02-no-card.toml` (production image has no card: every app works), `storage/v02-unusable-card.toml` (non-FAT card: `unusable card`, never formatted); `test_storage_core.c` | **Deferred to V1:** no card / non-FAT / faulty card on the breadboard |

The ADR-0008 shared-bus risk stays **open** (ARCHITECTURE §13): the emulator proves the
UI keeps rendering while a worker writes through the same storage code, not that the real
bus shares well.

### V0.3 (esp-emulator 0.44.0; production image and storage test image, RMT loopback)

Same environment as V0.2. Scenarios marked *loopback* run esp-emu with
`--rmt-loopback 0:2`: the first RMT TX channel's **baseband** symbols arrive at the first
RX channel with exact durations — no carrier, no LED, no light, no receiver (ADR-0019 §2,
VALIDATION §3.6). Nothing below is optical evidence.

| Date | Task | Metric | Value | Source |
| --- | --- | --- | --- | --- |
| 2026-10-24 | STARK-0307 | Free internal heap at boot, production image — **gate ≥ 200 kB** | 266 700 B (V0.2: 288 832 B; −22 132 B) | every production scenario |
| 2026-10-24 | STARK-0307 | Free internal heap at boot, storage test image (seeded FAT mounted) | 267 060 B | every storage scenario |
| 2026-10-24 | STARK-0307 | Static RAM (`.bss` + `.data`): `stark_ir` / `app_ir` / RMT driver | 7 734 B / 6 994 B / 317 B | `idf.py size-components` |
| 2026-10-24 | STARK-0307 | of which, `stark_ir`: IR task stack 3 072 B, capture ring 2 136 B, TX symbols 648 B, TX signal copy 532 B, RX work signal 532 B, RX symbols 520 B; `app_ir`: six full captures 3 192 B, shared worker buffer 2 332 B, browse list 784 B, sent file name 24 B (STARK-0305/0306 "As built") | — | `nm` |
| 2026-10-24 | STARK-0303 | Boot heap cost of the RX channel and IR task | −9 568 B (≈ 6.3 kB static, ≈ 3.2 kB RMT driver heap) | `diag: heap=` at boot |
| 2026-10-24 | STARK-0307 | Flash: `stark_ir` / `app_ir` / RMT driver (code + rodata) | 7 067 B / 3 127 B / 13 080 B | `idf.py size-components` |
| 2026-10-24 | STARK-0307 | Application binary (`stark-one.bin`) | 417 968 B (V0.2: 376 576 B; +41 392 B) | `idf.py build` |
| 2026-10-24 | STARK-0307 | Task stack never touched (fill-pattern dump after every protocol sent and received, saves included; storage image, loopback): IR task / app worker | 1 152 of 3 072 B / 2 356 of 4 096 B | GDB dump, `storage/v03-protocols`, `storage/v03-capture-replay` |
| 2026-10-24 | STARK-0307 | Root heap, first vs last stark_diag line over the 60 s V0.3 soak (10 replays, 2 saves, 3 browses, IR self-test, write test, settings) — **gate ≤ 1 kB** | 262 656 → 262 700 B (Δ 44 B) | `soak/v03-soak` (storage image, loopback) |
| 2026-10-24 | STARK-0304 | Heap across three IR self-test runs — **gate ± 256 B** | 276 084 → 276 084 B (Δ 0) | throwaway run, production image, loopback |
| 2026-10-24 | STARK-0307 | Event drops over the soaks and every scenario — **gate 0** | 0 | `test_emu.py` drops gate |
| 2026-10-24 | STARK-0307 | Host line coverage, pure sources | 1 853 / 1 857 (99.8 %); `stark_ir` pure core 571 / 571 (100 %: timing, NEC, bi-phase, RC5, RC6, SIRC, raw, symbols, capture, tx guard, names) | `test_host.sh --coverage` |
| 2026-10-24 | STARK-0302 | Spec-derived host vectors (`gen_ir_vectors.py`, not recorded) | NEC 23, NECX 21, RC5 21, RC6 24, SIRC-12/15/20 20 each; exhaustive round trips RC5 8 192, RC6 131 072, SIRC-20 1 048 576 frames; 10 000 fuzzed `.ir` inputs | host suite |

### V0.3 exit criteria — evidence (ROADMAP.md "V0.3", ADR-0019)

V0.3 completion is **IR software / protocol verified**. Every physical half is open until
the V1 prototype (ROADMAP V1 exit 9, `docs/bringup/v1-ir.md`) and is **not claimed** here.

| # | Criterion | Software evidence (V0.3) | Physical evidence |
| --- | --- | --- | --- |
| 1 | Codec host tests pass on ≥ 20 vectors per supported protocol | ≥ 20 spec-derived vectors per protocol (NEC, NECX, RC5, RC6-0, SIRC-12/15/20) with seeded jitter, ± 24 % / ± 26 % tolerance edges and malformed frames (`test_ir_nec.c`, `test_ir_codecs.c`); every protocol encoded → RMT TX → loopback → RMT RX → decoded on the production firmware (`v03-ir-loopback`, 7/7) and every seeded file replayed and received back with identical fields (`storage/v03-protocols`) | **Deferred to V1:** ≥ 20 vectors recorded from our own remotes per protocol |
| 2 | Capture, save, replay, device responds | `storage/v03-capture-replay`: a seeded RC6 frame captured by the IR app → saved as `ir_0001.ir` → listed first → parsed back → replayed after the confirmation → captured again with the same fields; `storage/v03-replay` (cancel, busy, invalid file); every transmit gated, confirmed and logged (`forbid_between`); `test_ui_ir.c`, `test_ir_tx_guard.c` | **Deferred to V1:** own remote → TSOP receiver → save → IR LED → own device responds; carrier and duty on a logic analyser |
| 3 | Raw replay of an unsupported protocol | `test_ir_raw.c` (round trip, strict parser, fuzzing); `storage/v03-capture-replay`: a signal no decoder accepts captured as `RAW n=13` → saved as `ir_0002.ir` → replayed → captured back with the same 13 durations | **Deferred to V1:** the same with an own remote of an unsupported protocol |

### V0.4 (esp-emulator 0.44.0; production image on an empty I²C bus, test image with the fake PN532 transport)

Same environment as V0.3. The production image's I²C bus is **empty** (esp-emu models no
I²C devices), so it only exercises the no-module path. The test image substitutes
`stark_nfc_transport` with the test-only fake PN532 (ADR-0020 §3). Nothing below is
evidence of a real tag, a PN532 on the bus, or an RF field.

| Date | Task | Metric | Value | Source |
| --- | --- | --- | --- | --- |
| 2026-10-25 | STARK-0405 | Free internal heap at boot, production image — **gate ≥ 200 kB** | 254 276 B (V0.3: 266 700 B) | every production scenario |
| 2026-10-25 | STARK-0405 | Free internal heap at boot, test image (fake PN532 detected, NFC task running) | 252 252 B | every storage scenario |
| 2026-10-25 | STARK-0402 | Boot heap cost of the I²C bus, PN532 device and driver state | −2 428 B | `diag: heap=` at boot |
| 2026-10-25 | STARK-0405 | Static RAM (`.bss` + `.data`): `stark_nfc` / `app_nfc` / I²C driver / transport | 7 369 B / 2 871 B / 838 B / 4 B | `idf.py size-components` |
| 2026-10-25 | STARK-0405 | of which, `stark_nfc`: NFC task stack 4 096 B, snapshot 908 B, the task's own read 908 B, write request 872 B, driver state 304 B; `app_nfc`: dump buffer 952 B, tag copy 908 B, rows and labels 652 B | — | `nm` |
| 2026-10-25 | STARK-0405 | Flash: `stark_nfc` / `app_nfc` / I²C driver + HAL (code + rodata) | 9 103 B / 2 870 B / 12 593 B | `idf.py size-components` |
| 2026-10-25 | STARK-0405 | Application binary (`stark-one.bin`) | 448 928 B (V0.3: 417 968 B; +30 960 B) | `idf.py build` |
| 2026-10-25 | STARK-0405 | Task stack never touched (fill-pattern dump after reads, a write, faults, a recovery and two dumps; test image): NFC task / app worker | 2 464 of 4 096 B / 2 044 of 4 096 B | GDB dump, `storage/v04-nfc-app`, `storage/v04-dump` |
| 2026-10-25 | STARK-0405 | Root heap, first vs last stark_diag line over the 60 s V0.4 soak (10 tag arrivals, 2 writes, 2 dumps, a module loss and recovery, an IR replay, a write test) — **gate ≤ 1 kB** | 247 848 → 247 892 B (Δ 44 B) | `soak/v04-soak` (test image, fake transport, RMT loopback) |
| 2026-10-25 | STARK-0405 | Event drops over the four soaks and every scenario — **gate 0** | 0 | `test_emu.py` drops gate |
| 2026-10-25 | STARK-0405 | Host line coverage, pure sources | 2 799 / 2 803 (99.9 %); `stark_nfc` pure core 946 / 946 (100 %: NDEF, PN532 frame and driver, identification, Type 2, reader, poll, writer, dump) | `test_host.sh --coverage` |
| 2026-10-25 | STARK-0405 | Spec-derived vectors (`gen_nfc_vectors.py`, not read from a tag) | 73 NDEF + 24 URI/Text vectors; 36 PN532 frame vectors (UM0701 examples asserted byte for byte); a 26-tag catalogue (NTAG213/215/216, Ultralight, EV1, Classic Mini/1K/4K, ISO-DEP, TLV layouts, a vanishing tag) | host suite |

### V0.4 exit criteria — evidence (ROADMAP.md "V0.4", ADR-0020)

V0.4 completion is **NFC software / protocol verified**. Every physical half is open
until the V1 prototype (ROADMAP V1 exit 10, `docs/bringup/v1-nfc.md`) and is **not
claimed** here.

| # | Criterion | Software evidence (V0.4) | Physical evidence |
| --- | --- | --- | --- |
| 1 | UID and technology of our own NTAG/ISO 14443A tags, ≥ 20/20 reads | 26 catalogue tags identified through the production reader + driver + fake (`test_nfc_reader.c`); 20/20 consecutive host reads identical with no driver errors; `storage/v04-reads`: the same NTAG213 arrives and leaves 20 times, read 20/20 with the same UID, technology and NDEF record, then NTAG215, read-only NTAG213, Ultralight, Classic 1K and ISO-DEP once each | **Deferred to V1:** 20/20 reads of each of our own tag types on the breadboard |
| 2 | Writes and reads back an NDEF URI record on a writable tag | `test_nfc_writer.c` (written, read back byte-identical, parsed; capacity boundary; read-only, wrong-tag and no-tag refusals with zero WRITEs; a vanish after each page; every WRITE inside the data area); `storage/v04-write-readback`: URIs written to an NTAG215 and an Ultralight after the confirmation and read back as the same URI (`nfcapp: record URI …`) | **Deferred to V1:** our own NTAG written and read back by the device and by a phone |
| 3 | Module absence detected, app disables itself | `v04-nfc-boot`, `v04-nfc-absent`, `v02-no-card` (production image, empty bus: `nfc: no module (Not found)`, the app shows "No NFC module", no field); host: absent, silent, NACKing and corrupt modules, module loss and recovery (`test_pn532.c`, `test_nfc_poll.c`, `storage/v04-nfc-app`) | **Deferred to V1:** a board without the module; the module unplugged while running |

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
| Settings survive a power cycle (V0.2) | pass | | |
| 100 kB microSD write test byte-identical while rendering (V0.2) | pass | | |
| Worst display band-flush stall during SD commands (V0.2) | (record) | | |
| Card pulled mid-write: error, no panic, remount (V0.2) | pass | | |
| No card / non-FAT / faulty card: device usable (V0.2) | pass | | |
| Panel + SD stable on SPI2 at the chosen clocks; SD MISO released when deselected (V0.2) | pass | | |
| IR carrier frequency per protocol on a logic analyser (V0.3) | 36 / 38 / 40 kHz ± 1 % | | |
| IR carrier duty (V0.3) | 33 % ± 3 % | | |
| IR LED current through the NPN driver (V0.3) | (record vs design) | | |
| ≥ 20 vectors per protocol recorded from our own remotes, decoded (V0.3) | pass | | |
| Own remote captured → saved → replayed → own device responds (V0.3) | pass | | |
| Raw replay of an unsupported own remote (V0.3) | pass | | |
| IR range / angle / ambient light / echo while transmitting (V0.3) | (record) | | |
| PN532 answers at 0x24 on GPIO 1/2; I²C waveform and pull-ups on a logic analyser (V0.4) | pass | | |
| PN532 detection + initialisation time from boot (V0.4) | (record) | | |
| ≥ 20/20 UID + technology reads of each of our own tag types (V0.4) | pass | | |
| NDEF URI written to our own NTAG, read back by the device and a phone (V0.4) | pass | | |
| Tag detection range and orientation; removal latency (V0.4) | (record) | | |
| Weak, noisy and two nearby tags; module absent / unplugged while running (V0.4) | (record) / pass | | |
| Current with the RF field on and off (V0.4) | (record) | | |

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
