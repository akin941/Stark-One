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

### V0.5 module bus (software evidence, ADR-0021)

| Date | Task | What | Value | Where |
| --- | --- | --- | --- | --- |
| 2026-11-05 | STARK-0505 | Production boot heap (`diag: heap=`), V0.4 254 276 B — **gate ≥ 200 kB** | 250 344 B (−3 932 B) | `v0-boot` |
| 2026-11-05 | STARK-0505 | Test image boot heap | 246 160 B | `storage/v05-soak` |
| 2026-11-05 | STARK-0505 | Firmware binary, V0.4 448 928 B | 456 656 B (+7 728 B) | `build/stark-one.bin` |
| 2026-11-05 | STARK-0505 | Static RAM: `stark_module` (task stack 2 560 B) / transport / `app_modules` | 3 012 B / 20 B / 856 B | `idf_size.py --archives` |
| 2026-11-05 | STARK-0505 | Flash: `stark_module` / transport / `app_modules` | 2 824 B / 307 B / 1 486 B | `idf_size.py --archives` |
| 2026-11-05 | STARK-0505 | Stack peak (fill-pattern dump after the V0.5 soak; test image): module task / NFC task, idle checks included | 1 468 of 2 560 B / 1 384 of 4 096 B in this run (V0.4's heavier run: 1 632 B) | GDB dump |
| 2026-11-05 | STARK-0505 | Root heap, first vs last `stark_diag` line over the 60 s V0.5 soak — **gate ≤ 1 kB** | 238 568 → 238 612 B (Δ 44 B) | `soak/v05-soak` |
| 2026-11-05 | STARK-0505 | Event drops over the V0.5 soak — **gate 0** | 0 | `test_emu.py` drops gate |
| 2026-11-05 | STARK-0505 | Host line coverage: `module_desc.c`, `module_core.c` | 100 % | `test_host.sh --coverage` |
| 2026-11-05 | STARK-0505 | Vectors (`gen_module_vectors.py`, never read from a real module) | 31-entry catalogue (14 parse, 3 of them unsupported combinations; 17 rejected), every bit flip, truncation at every length, 10 000 seeded mutations | host suite |

### V0.5 exit criteria — evidence (ROADMAP.md "V0.5", ADR-0021)

V0.5 completion is **software / module-protocol verified**. Every physical half is open
until the V1 prototype (ROADMAP V1 exit 11, `docs/bringup/v1-modules.md`) and is **not
claimed** here.

| # | Criterion | Software evidence (V0.5) | Physical evidence |
| --- | --- | --- | --- |
| 1 | Plugging a module changes the app list without a firmware change | `storage/v05-hotplug` (same image: reader leaves, NFC dimmed and skipped; EEPROM declaring NFC confirms nothing; reader returns, row back and opens); `test_ui_app_caps.c`, `test_module_core.c` | Open: check 21 |
| 2 | Unknown/absent EEPROM gives a generic entry, never a boot failure | `v05-empty-bus` (production: slot 0 `generic (bad magic)`, slot 1 `no id`); every corrupt catalogue entry in `test_module_core.c` | Open: checks 3, 4 |
| 3 | Two modules share SPI without corrupting the display | **Not met in software: it is a V1 gate.** Software half: two wired slots in `test_module_claim.c`; `storage/v05-spi-traffic` (one stand-in driver, 7 177 transfers alongside rendering, `overruns=0`, stale lease on removal) | Open: checks 12–15 |

### V0.6 Sub-GHz module (software evidence, ADR-0022)

| Date | Task | What | Value | Where |
| --- | --- | --- | --- | --- |
| 2026-11-23 | STARK-0601 | Production boot heap (`diag: heap=`), V0.5 250 344 B — **gate ≥ 200 kB** | 245 976 B (−4 368 B) | `v0-boot`, `v06-subghz-absent` |
| 2026-11-23 | STARK-0601 | Test image boot heap (V0.5 246 160 B in `storage/v05-soak`'s first line) | 225 392 B (−20 768 B: the service, plus 16 800 B of static scene and recording buffers in the test-only `fake_cc1101.c`; production does not link them) | `storage/v06-detect` |
| 2026-11-23 | STARK-0601 | Firmware binary, V0.5 456 656 B | 461 152 B (+4 496 B) | `build/stark-one.bin` |
| 2026-11-23 | STARK-0601 | Static RAM: `stark_subghz` (task stack 4 096 B) / `stark_subghz_transport` — **budget ≤ 5 kB together** | 4 313 B / 21 B | `idf.py size-components` |
| 2026-11-23 | STARK-0601 | Flash: `stark_subghz` (code 2 455 B + rodata 296 B) / `stark_subghz_transport` — **budget ≤ 8 kB together** | 2 751 B / 223 B | `idf.py size-components` |
| 2026-11-23 | STARK-0601 | Root heap, first vs last `stark_diag` line over each 60 s soak — **gate ≤ 1 kB** | v01 248 176 → 248 212 B (Δ 36 B); v02 217 800 → 217 832 B (Δ 32 B); v03, v04, v05 217 800 → 217 844 B (Δ 44 B) | `soak/v01…v05-soak` |
| 2026-11-23 | STARK-0601 | Event drops over the five soaks — **gate 0** | 0 | `test_emu.py` drops gate |
| 2026-11-23 | STARK-0601 | Host line coverage: `cc1101_regs.c`, `cc1101_drv.c`, `subghz_svc.c`, and `module_core.c` / `module_desc.c` with the new bit | 100 % each | `test_host.sh --coverage` |
| 2026-11-23 | STARK-0601 | Host suite | 62 test programs; the four new ones (`cc1101_regs`, `cc1101_drv`, `fake_cc1101`, `subghz_service`) run 9, 15, 13 and 20 tests under ASan and UBSan | `test_host.sh` |
| 2026-11-23 | STARK-0601 | Module catalogue (`gen_module_vectors.py`) | 33 entries: entry 32 the CC1101 module, entry 33 an unknown-bit-6 vector; entry 10 `unknown_bits` is now an unsupported combination | host suite |

| 2026-11-23 | STARK-0602 | Production boot heap (STARK-0601 245 976 B) — **gate ≥ 200 kB** | 245 500 B (−476 B) | `v0-boot`, `v06-subghz-absent` |
| 2026-11-23 | STARK-0602 | Test image boot heap (STARK-0601 225 392 B) | 224 916 B (−476 B: the service's lock and measurement state; the fake's air and scan code are small) | `storage/v06-scan` |
| 2026-11-23 | STARK-0602 | Firmware binary (STARK-0601 461 152 B) | 465 088 B (+3 936 B) | `build/stark-one.bin` |
| 2026-11-23 | STARK-0602 | Static RAM: `stark_subghz` (task stack 4 096 B), STARK-0601 4 313 B | 4 481 B (+168 B: the mutex, the status and the scan state) | `idf.py size-components` |
| 2026-11-23 | STARK-0602 | Flash: `stark_subghz` (code + rodata), STARK-0601 2 751 B | 4 677 B (+1 926 B: RSSI conversion and text, the scan, the meter and the driver's retune) | `idf.py size-components` |
| 2026-11-23 | STARK-0602 | `app_subghz` static RAM — **budget ≤ 2 kB** / flash — **budget ≤ 4 kB** | 265 B / 1 663 B | `idf.py size-components` |
| 2026-11-23 | STARK-0602 | Host line coverage: `subghz_rssi.c`, `cc1101_drv.c`, `subghz_svc.c` (with `cc1101_regs.c`, `module_core.c`, `module_desc.c`) | 100 % each | `test_host.sh --coverage` |
| 2026-11-23 | STARK-0602 | Host suite | 65 test programs; the new ones run `test_subghz_rssi` 14, `test_subghz_measure` 27, `test_ui_subghz` 7 tests under ASan and UBSan; `test_cc1101_drv` 19 | `test_host.sh` |
| 2026-11-23 | STARK-0602 | Scan sweep on the host (injected clock, 69 channels × (1 ms settle + 1 ms step wait)) — **budget ≤ 500 ms** | ≤ 140 ms | `test_a_sweep_takes_at_most_500_ms` |
| 2026-11-23 | STARK-0603 | Production boot heap, first `diag: heap=` (STARK-0602 245 500 B) — **gate ≥ 200 kB** | 243 164 B (−2 336 B: the capture state and the 128-symbol scratch in the service, the app's rows and list; the 8 kB symbol buffer is allocated only while the app captures or opens a file) | `v0-boot`, `v06-subghz-absent` |
| 2026-11-23 | STARK-0603 | Test image boot heap, first `diag: heap=` (STARK-0602 224 916 B) — **gate ≥ 200 kB** | 239 732 B (+14 816 B: the fake CC1101's 16 800 B of static scene and recording buffers are gone — the scene is the catalogue in flash, the recording is allocated at the first STX — less the new static state) | `storage/v06-capture` |
| 2026-11-23 | STARK-0603 | Test image heap while capturing and saving (the 8 kB buffer, the app worker, the card), minimum over the scenario — **gate ≥ 200 kB** | 223 988 B at the first save, minimum 219 720 B | `storage/v06-capture` |
| 2026-11-23 | STARK-0603 | Firmware binary (STARK-0602 465 088 B) | 474 272 B (+9 184 B) | `build/stark-one.bin` |
| 2026-11-23 | STARK-0603 | Static RAM: `stark_subghz` (task stack 4 096 B), STARK-0602 4 481 B | 5 049 B (+568 B: the 128-symbol scratch (512 B) and the capture state) | `idf.py size-components` |
| 2026-11-23 | STARK-0603 | Flash: `stark_subghz` (code + rodata), STARK-0602 4 677 B — **budget ≤ 4 kB for the task** | 8 356 B (+3 679 B: `subghz_raw.c` 2 770 B, the capture mode and its logs 0.9 kB); `app_subghz` 1 663 B → 5 670 B (+4 007 B: the capture, saved and file views, the histogram rows, the worker I/O); the whole task +7.7 kB. The format core alone is inside the budget; the budget did not count the service and the three new views | `idf.py size-components`, `size-files` |
| 2026-11-23 | STARK-0603 | `app_subghz` static RAM — **budget ≤ 2 kB**, STARK-0602 265 B | 1 665 B (the rows' text, the saved list, the open file's description) | `idf.py size-components` |
| 2026-11-23 | STARK-0603 | Capture buffer — **budget 2048 × 4 B, the only large one** | 8 192 B, on the heap while the app captures or has a file open (none at boot, none after the app stops) | `subghz_files.c` |
| 2026-11-23 | STARK-0603 | Root heap, first vs last `stark_diag` line over each 60 s soak — **gate ≤ 1 kB** | v01 245 364 → 245 400 B (Δ 36 B); v02 232 140 → 232 172 B (Δ 32 B); v03, v04, v05 232 140 → 232 184 B (Δ 44 B) | `soak/v01…v05-soak` |
| 2026-11-23 | STARK-0603 | Event drops over those five soaks — **gate 0** | 0 | `test_emu.py` drops gate |
| 2026-11-23 | STARK-0603 | Host line coverage: `subghz_raw.c` (AC 4), `subghz_svc.c`, `cc1101_drv.c`, `cc1101_regs.c`, `subghz_rssi.c` | 100 % each | `test_host.sh --coverage` |
| 2026-11-23 | STARK-0603 | Host suite | 67 test programs; the new ones run `test_subghz_raw` 27 (10 000 seeded mutations of three valid files against a guarded buffer, every malformed case, every truncation before the last line, files read in pieces of 1…300 bytes, the generator's scene expectations, round trip of every scene through four chunk sizes), `test_subghz_capture` 21 and `test_ui_subghz` 12 tests under ASan and UBSan; `test_fake_cc1101` 13 | `test_host.sh` |
| 2026-11-23 | STARK-0603 | Scene timing through the production pipeline (AC 2) | Every symbol of the three spec-derived scenes equal to the scene's, to the microsecond, on the host (`test_scene_timing_matches_per_symbol`, fake chip → service → file → parser) and by fingerprint on the test image: capture, saved file and re-read file all `0x78a82ac5` = the generator's, histogram `400 µs ×92, 1200 µs ×96, 12400 µs ×4`; the 2400-symbol scene ends at 2048 symbols (`0x27a9fdc5`) | `storage/v06-capture`, `test_subghz_capture` |
| 2026-11-23 | STARK-0603 | Emulator wall time of `storage/v06-capture` (22 s of virtual time) | 354 s | `test_emu.sh --image storage` |
| 2026-11-23 | STARK-0603 | Scene catalogue (`gen_subghz_vectors.py`) | 5 scenes (`pwm24`, `ppm24`, `manchester32`, `dense`, `slow`) with their expected capture (symbols, µs, FNV-1a fingerprint, histogram) computed independently of the firmware; 21 script unit tests | host suite, script unit tests |
| 2026-11-23 | STARK-0604 | Production boot heap, first `diag: heap=` (STARK-0603 243 164 B) — **gate ≥ 200 kB** | 242 924 B (−240 B: the app's 232-byte analysis result) | `v0-boot`, `v06-subghz-absent` |
| 2026-11-23 | STARK-0604 | Test image boot heap, first `diag: heap=` (STARK-0603 239 732 B) — **gate ≥ 200 kB** | 239 476 B (−256 B); minimum 219 464 B while capturing, saving and decoding | `storage/v06-decode` |
| 2026-11-23 | STARK-0604 | Firmware binary (STARK-0603 474 272 B) | 477 712 B (+3 440 B) | `build/stark-one.bin` |
| 2026-11-23 | STARK-0604 | Flash: `stark_subghz` (code + rodata), STARK-0603 8 356 B — **budget ≤ 4 kB for the task** | 10 294 B (+1 938 B: `subghz_decode.c`); `app_subghz` 5 670 B → 6 717 B (+1 047 B: the analysis rows and log) | `idf.py size-components` |
| 2026-11-23 | STARK-0604 | Static RAM — **budget: no static buffer beyond 256 B** | `stark_subghz` 5 049 B (unchanged: the decoder has no state); `app_subghz` 1 665 B → 1 906 B (+241 B, the 232-byte result) | `idf.py size-components` |
| 2026-11-23 | STARK-0604 | Stack of `subghz_decode()` with its callee (host aarch64 `-O2`, indicative) | about 340 B (208 B + 128 B frames, the histogram inside the first) | `clang -S` |
| 2026-11-23 | STARK-0604 | Host line coverage: `subghz_decode.c` (AC 2), and the other Sub-GHz cores | 100 % each | `test_host.sh --coverage` |
| 2026-11-23 | STARK-0604 | Host suite | 68 test programs; `test_subghz_decode` runs 18 tests in 1 s under ASan and UBSan (every vector against the reference to the µs, 10 000 seeded random captures on exactly sized buffers, hostile 2048-symbol streams, the burst-gap edge); `test_ui_subghz` 16; 38 generator unit tests (111 script tests in all) | `test_host.sh`, script unit tests |
| 2026-11-23 | STARK-0604 | Decoder vectors (`gen_subghz_vectors.py`, host only, 185 kB of header) | 72 jittered (24 per kind, 8–128 bits, ±10 %), 3 exact, 28 tolerance edges, 12 bit-count limits, 16 noise, 48 mutated, and the named negatives; the independent reference reads every one as it was built | host suite, script unit tests |
| 2026-11-23 | STARK-0604 | Emulator `storage/v06-decode` (25 s of virtual time): the pulse-distance scene decodes to `ppm short=400 long=1200 bits=24 hex=5a3cc3 x4`, live and re-read from `rf_0001.rf` | passes; wall time 278 s | `test_emu.sh --image storage` |
| 2026-11-23 | STARK-0604 | Root heap, first vs last `stark_diag` line over each 60 s soak — **gate ≤ 1 kB**; event drops — **gate 0** | v01 245 124 → 245 160 B (Δ 36 B); v02 231 884 → 231 916 B (Δ 32 B); v03, v04, v05 231 884 → 231 928 B (Δ 44 B); drops 0 | `soak/v01…v05-soak` |
| 2026-11-23 | STARK-0605 | Production boot heap, first `diag: heap=` (STARK-0604 242 924 B) — **gate ≥ 200 kB** | 240 732 B (−2 192 B: the 1 536-byte duty ledger and the send state) | `v0-boot`, `v06-subghz-absent` |
| 2026-11-23 | STARK-0605 | Test image boot heap, first `diag: heap=` (STARK-0604 239 476 B) — **gate ≥ 200 kB** | 237 812 B (−1 664 B); minimum 209 092 B over the scenario (the app's 8 kB buffer, the fake's 8.4 kB recording allocated at the first `STX`) | `storage/v06-replay` |
| 2026-11-23 | STARK-0605 | Firmware binary (STARK-0604 477 712 B) | 483 216 B (+5 504 B) | `build/stark-one.bin` |
| 2026-11-23 | STARK-0605 | Flash: `stark_subghz` (code + rodata), STARK-0604 10 294 B — **budget ≤ 4 kB for the task** | 12 356 B (+2 062 B: `subghz_txpolicy.c`, the transmit path of `subghz_svc.c`, `cc1101_tx_start`); `app_subghz` 6 717 B → 7 798 B (+1 081 B: `subghz_send.c`, the Replay row); `stark_subghz_transport` 1 393 B (with the RMT transmit path); the image grew 5 504 B in all, 1.5 kB over the task's 4 kB (the refusal texts and log lines, and the transmit path itself) | `idf.py size-components` |
| 2026-11-23 | STARK-0605 | Static RAM | `stark_subghz` 5 049 B → 6 665 B (+1 616 B: the 192-entry duty ledger is 1 536 B); `app_subghz` 1 906 B → 1 947 B (+41 B); `stark_subghz_transport` 397 B (the RMT receive buffer and queue, and the transmit encoder's two counters) | `idf.py size-components` |
| 2026-11-23 | STARK-0605 | Host line coverage: `subghz_txpolicy.c`, `subghz_svc.c`, `cc1101_drv.c` and the other Sub-GHz cores (AC) | 100 % each | `test_host.sh --coverage` |
| 2026-11-23 | STARK-0605 | Host suite | 70 test programs; `test_subghz_txpolicy` 20 tests, `test_subghz_tx` 30 (a failure injected at each of the 92 transfers of a send and a dead bus from each of them, two hours of a sender asking every 250 ms three times over, one across the 32-bit clock wrap), `test_cc1101_drv` 23, `test_ui_subghz` 22, all under ASan and UBSan | `test_host.sh` |
| 2026-11-23 | STARK-0605 | Emulator `storage/v06-replay` (31 s of virtual time): refused inside the boot lock (`subghz: tx refused: offtime`), cancel sends nothing, one OK sends `subghz: tx rf_0001.rf 433920000 Hz 192 sym 202 ms`, an immediate second ask is refused, and the fake's own receiver captures back 192 symbols with the file's fingerprint `0x78a82ac5` | passes; wall time 207 s | `test_emu.sh --image storage` |
| 2026-11-23 | STARK-0605 | Root heap, first vs last `stark_diag` line over each 60 s soak — **gate ≤ 1 kB**; event drops — **gate 0** | v01 242 932 → 242 968 B (Δ 36 B); v02 230 220 → 230 252 B (Δ 32 B); v03, v04, v05 230 220 → 230 264 B (Δ 44 B); drops 0 | `soak/v01…v05-soak` |
| 2026-11-23 | STARK-0606 | Production boot heap, first `diag: heap=` (V0.5 250 344 B, STARK-0605 240 732 B) — **gate ≥ 200 kB** | 240 732 B (−9 612 B from V0.5; no firmware source changed in this task) | `v0-boot`, `v06-subghz-absent` |
| 2026-11-23 | STARK-0606 | Test image boot heap, first `diag: heap=` (STARK-0605 237 812 B) | 237 812 B (unchanged: the test-only change is code in the fake, no static state); minimum 209 092 B over the soak (the app's 8 kB capture buffer open during a replay) | `soak/v06-soak` |
| 2026-11-23 | STARK-0606 | Firmware binary (V0.5 456 656 B, STARK-0605 483 216 B) | 483 216 B (+26 560 B from V0.5; byte-identical size to STARK-0605: no firmware source changed); test image 500 288 B | `build/stark-one.bin`, `build/emu-storage/stark-one-emu-storage.bin` |
| 2026-11-23 | STARK-0606 | V0.6 flash, code + rodata (V0.5 had none of these) | `stark_subghz` 12 356 B, `app_subghz` 7 798 B, `stark_subghz_transport` 1 393 B: 21 547 B in all; the image is +26 560 B from V0.5, the rest being the module catalogue and capability changes, the launcher row and the registry | `idf.py size-components` |
| 2026-11-23 | STARK-0606 | V0.6 static RAM (.bss + .data) | `stark_subghz` 6 665 B (of which the task stack 4 096 B, the duty ledger 1 536 B, the 128-symbol scratch 512 B), `app_subghz` 1 947 B, `stark_subghz_transport` 397 B: 9 009 B in all. The 8 kB capture buffer is heap, held only while the app captures or has a file open | `idf.py size-components` |
| 2026-11-23 | STARK-0606 | **Task stack peak** over the 60 s V0.6 soak (two passes of every view, two saves, two decodes, two transmissions, two module losses; test image), untouched bytes counted from the stack's low end in a fill-pattern dump at the end: `stark_subghz` task | **1 600 of 4 096 B** (2 496 B never touched; 39 %). 0601's "4 096 B" is the stack's size, never a measurement | GDB dump, `soak/v06-soak` |
| 2026-11-23 | STARK-0606 | Other task stacks in the same run, same method: app worker / module task / NFC task / IR task | 2 484 of 4 096 B (1 612 untouched; saves and file reads run here) / 1 472 of 2 560 B (1 088 untouched; V0.5 1 468 B) / 680 of 4 096 B / 728 of 3 072 B (the last two idle: the NFC and IR apps are never opened in this soak) | GDB dump, `soak/v06-soak` |
| 2026-11-23 | STARK-0606 | Root heap, first vs last `stark_diag` line over the new 60 s V0.6 soak — **gate ≤ 1 kB**; event drops — **gate 0** | 230 220 → 230 264 B (Δ 44 B); drops 0 on all seven periodic lines (the first at 1 s, then every 10 s); heap 222 068 B at the lines taken inside the app (the capture buffer), back to 230 264 B at the launcher both times | `soak/v06-soak` |
| 2026-11-23 | STARK-0606 | What the V0.6 soak did (log lines) | 2 module losses and re-finds, 2 meter sessions (`-40`, `-70`, `-55` dBm), 2 band scans (`peak 433.925 MHz -55 dBm`), 2 captures (192 symbols, `0x78a82ac5`), 2 saves (`rf_0001.rf`, `rf_0002.rf`), 2 decodes (`pwm short=400 long=1200 bits=24 hex=a5c3e1 x4`), 2 transmissions (`subghz: tx rf_000N.rf 433920000 Hz 192 sym 202 ms`, one per confirmation), 2 refusals (`subghz: tx refused: offtime`) | `soak/v06-soak` |
| 2026-11-23 | STARK-0606 | Emulator wall time of `soak/v06-soak` (62.5 s of virtual time) | 525 s | `test_emu.sh --image storage` |
| 2026-11-23 | STARK-0606 | Root heap, first vs last `stark_diag` line over each earlier 60 s soak — **gate ≤ 1 kB**; event drops — **gate 0** | v01 242 932 → 242 968 B (Δ 36 B); v02 230 220 → 230 252 B (Δ 32 B); v03, v04, v05 230 220 → 230 264 B (Δ 44 B); drops 0 | `soak/v01…v05-soak` |
| 2026-11-23 | STARK-0606 | Host line coverage: every Sub-GHz pure core and the module core | 100 % lines each: `cc1101_regs.c`, `cc1101_drv.c`, `subghz_rssi.c`, `subghz_raw.c`, `subghz_decode.c`, `subghz_txpolicy.c`, `subghz_svc.c`, `module_core.c`, `module_desc.c` | `test_host.sh --coverage` |
| 2026-11-23 | STARK-0606 | Host suite | 70 test programs, all pass under ASan and UBSan (`test_fake_cc1101` gained one test, the recording's release); no other test changed | `test_host.sh` |

### V0.6 exit criteria — evidence (ROADMAP.md "V0.6", ADR-0022)

V0.6 completion is **software / RF-protocol verified**. Every physical half is open until
the V1 prototype (ROADMAP V1 exit 12, `docs/bringup/v1-subghz.md`) and is **not claimed**
here: no radio has transmitted or received, and nothing below measures a signal.

| # | Criterion | Software evidence (V0.6) | Physical evidence |
| --- | --- | --- | --- |
| 1 | RSSI/scan view shows our own transmitter and tracks distance sensibly | `test_subghz_rssi` (conversion vectors), `test_subghz_measure` (the sweep finds the injected carrier's channel in ≤ 140 ms of waiting); `storage/v06-scan` (the meter follows the fake's air through −40, −70, −55 dBm; the scan reports `peak 433.925 MHz -40 dBm`; the radio is idle after both); both passes of `soak/v06-soak` | Open: checks 10–13 |
| 2 | Raw capture of an own device's transmission reproduces its documented timing | `test_subghz_capture` (every symbol of the spec-derived scenes equal to the generator's to the µs, through fake chip → service → file → parser), `test_subghz_raw` (strict format, 10 000 mutations), `test_subghz_decode` (every vector against an independent reference); `storage/v06-capture` (fingerprint `0x78a82ac5` live, saved and re-read; histogram `400 µs ×92, 1200 µs ×96, 12400 µs ×4`), `storage/v06-decode`; the soak's captures and decodes | Open: checks 14–17 |
| 3 | Replay of our own capture triggers our own receiver | `test_subghz_tx` (the file is on GDO0 symbol for symbol; the own receiver hears it); `storage/v06-replay` (192 symbols captured back with the file's own fingerprint `0x78a82ac5`) | Open: checks 20, 24 |
| 4 | TX is impossible without an explicit confirmation and respects the duty-cycle cap | `test_subghz_txpolicy` (20 tests, every limit at its exact boundary), `test_subghz_tx` (30 tests: every refusal with nothing on GDO0, a failure at every transfer of a send, two hours of a greedy sender held to 360 s per hour and 9× off-time, `PATABLE` only ever 0x00 or 0x60); `storage/v06-replay` (boot lock, cancel sends nothing, one OK one transmission, a second ask refused); the soak (2 sends, 2 refusals, none before a confirmation) | Open: checks 18–23, 25 |

**Not claimed:** the CC1101 answering on real SPI, the `CHIP_RDYn` wait, the analogue
register values, the production RMT receive and transmit paths on GDO2 and GDO0, RSSI against
a real level, any real remote's timing (the 20 ms frame-end low in `rx_take` is a guess until
check 14), conducted or radiated power, on-air time, spurious emissions, antenna and matching.

**How the stack peak is read.** FreeRTOS fills a static task stack with 0xa5 when the task is
created. After the soak ended at the launcher, a GDB script (esp-emu halted at its sampling
breakpoint) counted the bytes still holding 0xa5 from the low end of each stack array, which is
the part the task never reached (`while $p[$n] == 0xa5`, `$p = 'stark_subghz.c'::s_stack`, likewise
`app_worker.c`, `stark_module.c`, `stark_nfc.c`, `stark_ir.c`). Peak = size − untouched. It covers
what the soak exercised; interrupt and ISR stacks are not part of it.

Software evidence only: no CC1101, antenna or SPI signal exists before the V1 prototype
(ROADMAP V1 exit 12, `docs/bringup/v1-subghz.md`). The production RMT receive and transmit
paths for GDO2 and GDO0 are built but not exercised by any test.

### V0.7 lab tools (software evidence, ADR-0023)

| Date | Task | What | Value | Where |
| --- | --- | --- | --- | --- |
| 2026-11-24 | STARK-0701 | Production boot heap, first `diag: heap=` (STARK-0606 240 732 B) — **gate ≥ 200 kB, V0.7 total cost ≤ 16 kB** | 241 132 B (+400 B). Static RAM grew by 432 B (`.bss` 65 880 → 66 312 B); the heap figure still rose because the 16 kB IRAM is full and the linker's overflow into DIRAM shifted by 344 B (image segments 2 and 4), so it is a layout effect and not a saving. Cost against the budget: 432 B | `v0-boot`, `v07-lab-boot` |
| 2026-11-24 | STARK-0701 | Test image boot heap (STARK-0606 237 812 B) | 236 340 B (−1 472 B: the lab's 413 B plus the pad model's static array of 49 pads, about 1.2 kB, in the test-only `fake_lab.c`; production does not link it) | `storage/v07-pins` |
| 2026-11-24 | STARK-0701 | Firmware binary (STARK-0606 483 216 B) | 486 528 B (+3 312 B) | `build/stark-one.bin` |
| 2026-11-24 | STARK-0701 | Static RAM: `stark_lab` / `stark_lab_transport` — **budget ≤ 1.5 kB together**; `stark_hal` (+8 B: the pad tracking mask) | 413 B / 1 B (414 B) | `idf.py size-components` |
| 2026-11-24 | STARK-0701 | Flash (code + rodata): `stark_lab` / `stark_lab_transport` — **budget ≤ 3 kB together**; also `stark_board` +451 B (the pin classes), `stark_hal` +427 B (the three pad calls), `main` +60 B | 1 132 B / 345 B (1 477 B) | `idf.py size-components` |
| 2026-11-24 | STARK-0701 | FreeRTOS tasks added — **ADR-0023: none** | 0 (the service runs on its caller's task; the mutex is a static object) | `stark_lab.c` |
| 2026-11-24 | STARK-0701 | IRAM: 16 384 of 16 384 B used before and after this task (0 B free) | Not a cost of this task, but a fact for STARK-0703: IDF places IRAM code that does not fit in the instruction region of DIRAM, so the edge-capture ISR will cost heap (about its own size) and not fail the link | `idf.py size` |
| 2026-11-24 | STARK-0701 | Host line coverage: `stark_board.c`, `lab_pins.c`, `lab_transport_hal.c`, `fake_lab.c` | 100 % each (459 of 459 lines with the transport header) | `test_host.sh --coverage` |
| 2026-11-24 | STARK-0701 | Host suite | 74 test programs (70 before); the new ones run `test_board_pins` 8, `test_fake_lab` 11, `test_lab_pins` 24, `test_lab_service` 9 tests, and `test_hal` gained 2, under ASan and UBSan | `test_host.sh` |
| 2026-11-24 | STARK-0701 | Emulator wall time (docker, three to four scenarios at once) | `v07-lab-boot` 55 s alone; `storage/v07-pins` (12 s of virtual time) 147 s | `test_emu.sh` |
| 2026-11-24 | STARK-0701 | Root heap, first vs last `stark_diag` line over each 60 s soak — **gate ≤ 1 kB** | v01 243 332 → 243 368 B (Δ 36 B); v02 228 748 → 228 780 B (Δ 32 B); v03, v04, v05, v06 228 748 → 228 792 B (Δ 44 B) | `soak/v01…v06-soak` |
| 2026-11-24 | STARK-0701 | Event drops over the six soaks — **gate 0** | 0 | `test_emu.py` drops gate |
| 2026-11-24 | STARK-0701 | Scenarios, whole set: production image 21 (every `test/emu/*.toml`, including `v07-lab-boot`), test image 27 (every `test/emu/storage/*.toml`, including `v07-pins`), soaks v01 to v06 | all pass; the longest, `soak/v06-soak`, took 717 s of wall time with four emulators running at once | `test_emu.sh` |
| 2026-11-24 | STARK-0701 | esp-emu 0.44.0 probes (production image, throwaway task, not committed) | Output read-back, self-driven GPIO interrupt and the 0x08–0x77 I²C sweep work; pull-down reads and LEDC on a pad do not; the EEPROM at 0x50 accepts writes. Every row in docs/VALIDATION.md §2 | `docs/VALIDATION.md` |
| 2026-11-24 | STARK-0702 | Production boot heap, first `diag: heap=` (STARK-0701 241 132 B) — **gate ≥ 200 kB, V0.7 total cost ≤ 16 kB** | 240 128 B (−1 004 B; −604 B against the V0.6 close, 240 732 B). Static RAM grew by 976 B (`.bss` 66 312 → 67 288 B); the IRAM is still full, so the DIRAM overflow layout moves with the code, and the heap figure includes that | `v0-boot`, `v07-lab-boot`, `v07-gpio-prod` |
| 2026-11-24 | STARK-0702 | Test image boot heap (STARK-0701 236 340 B) | 234 904 B (−1 436 B: the production cost above plus the pad model's PWM fields and the pokes' state, about 0.4 kB, in the test-only fake) | `storage/v07-gpio` |
| 2026-11-24 | STARK-0702 | Firmware binary (STARK-0701 486 528 B) | 495 856 B (+9 328 B: `app_gpio` 4 378 B, `stark_lab` +3 076 B, `stark_lab_transport` +326 B, the HAL's three calls and LEDC tracking, the launcher row) | `build/stark-one.bin` |
| 2026-11-24 | STARK-0702 | Static RAM: `app_gpio` — **budget ≤ 2 kB** / `stark_lab` (413 B at STARK-0701) / `stark_lab_transport` / `stark_hal` | 786 B / 605 B (+192: the per-pin mode, level and PWM values and the table index) / 2 B / 8 B | `idf.py size-components` |
| 2026-11-24 | STARK-0702 | Flash (code + rodata): `app_gpio` — **budget ≤ 7 kB** / `stark_lab` / `stark_lab_transport` / `stark_hal` | 4 378 B / 4 208 B / 671 B / 1 003 B | `idf.py size-components` |
| 2026-11-24 | STARK-0702 | FreeRTOS tasks added — **ADR-0023: none** | 0 (the pulse runs on the app's existing worker; the cancel is a volatile flag) | `app_gpio.c` |
| 2026-11-24 | STARK-0702 | PWM arithmetic (`lab_pwm_plan()`: 10-bit duty, 80 MHz APB clock, Q8 divider), swept over all 39 901 integers from 100 to 40 000 Hz | every divider inside 1.0 to 1023.996; worst frequency error 997 ppm at 39 960 Hz (0.1 %), bound 0.2 % by construction; 100 Hz gives divider 781.25 exactly, 40 kHz gives 1.953125 exactly; 101 distinct duty tick counts for 0 to 100 %; 100 % is 1023 of 1024 ticks. Arithmetic only: the waveform is a V1 logic-analyser check | `test_lab_pwm` |
| 2026-11-24 | STARK-0702 | Host line coverage: `lab_pins.c`, `lab_pwm.c`, `lab_transport_hal.c`, `fake_lab.c`, `stark_board.c`, `gpio_views.c` | 100 % each | `test_host.sh --coverage` |
| 2026-11-24 | STARK-0702 | Host suite | 77 test programs (74 at STARK-0701): `test_lab_pwm` 8, `test_gpio_views` 10 and `test_ui_gpio` 17 tests are new; `test_lab_pins` 24 → 34, `test_lab_service` 9 → 17, `test_fake_lab` 11 → 16 and `test_hal` 13 → 18 tests, under ASan and UBSan | `test_host.sh` |
| 2026-11-24 | STARK-0702 | Emulator wall time (docker, up to six scenarios at once) | `v07-gpio-prod` 38 s; `storage/v07-gpio` (17.5 s of virtual time) 148 s and `v07-gpio-pwm` 144 s with four to six emulators running (251 s and 296 s in a first run under heavier load), among the longest of the set after the soaks, which is why the scenario is two files | `test_emu.sh` |
| 2026-11-24 | STARK-0702 | Root heap, first vs last `stark_diag` line over each 60 s soak — **gate ≤ 1 kB** | v01 242 328 → 242 364 B (Δ 36 B); v02 227 452 → 227 332 B (Δ 120 B); v03, v04, v05, v06 227 452 → 227 356 B (Δ 96 B). None of the soaks opens the GPIO app | `soak/v01…v06-soak` |
| 2026-11-24 | STARK-0702 | Event drops over the six soaks — **gate 0** | 0 | `test_emu.py` drops gate |
| 2026-11-24 | STARK-0702 | Scenarios, whole set: production image 22 (every `test/emu/*.toml`, including `v07-gpio-prod`), test image 29 (every `test/emu/storage/*.toml`, including `v07-gpio` and `v07-gpio-pwm`), soaks v01 to v06 | all pass | `test_emu.sh` |
| 2026-11-24 | STARK-0703 | Production boot heap, first `diag: heap=` (STARK-0702 240 128 B) — **gate ≥ 200 kB, V0.7 total cost ≤ 16 kB** | 239 776 B (−352 B; −956 B against the V0.6 close, 240 732 B). Static RAM grew by about 100 B; the IRAM is full, so the DIRAM overflow layout moves with the code and the heap figure includes the 84 B edge handler that now overflows into it | `v0-boot`, `v07-measure-prod` |
| 2026-11-24 | STARK-0703 | Test image boot heap (STARK-0702 234 904 B) | 234 808 B (−96 B) | `storage/v07-measure` |
| 2026-11-24 | STARK-0703 | Firmware binary (STARK-0702 495 856 B) | 500 352 B (+4 496 B) | `build/stark-one.bin` |
| 2026-11-24 | STARK-0703 | Static RAM — **budget ≤ 0.5 kB together**: `app_gpio` (786 B at STARK-0702) / `stark_lab` (605 B) / `stark_lab_transport` (2 B) / `stark_hal` (8 B) | 853 B (+67: the result text and the job's result) / 613 B (+8) / 2 B / 30 B (+22: the capture's ring pointer, counters and flags) — +97 B | `idf.py size-components` |
| 2026-11-24 | STARK-0703 | Flash (code + rodata) — **budget ≤ 4 kB together**: `app_gpio` (4 378 B) / `stark_lab` (4 208 B) / `stark_lab_transport` (671 B) / `stark_hal` (1 003 B) | 4 963 B (+585) / 6 071 B (+1 863: `lab_meas.c` and the service call) / 765 B (+94) / 1 535 B (+532) plus 84 B of IRAM — 3 074 B and the 84 B | `idf.py size-components` |
| 2026-11-24 | STARK-0703 | IRAM — the open question of the STARK-0701 finding | 16 384 of 16 384 B used (0 B free) before and after; the edge handler is 84 B of it (`libstark_hal.a`), the rest of what no longer fits overflows into DIRAM, so the cost is heap and is inside the boot-heap row. Decision: IRAM, because a flash operation must not make a gap that reads as a slow signal (ADR-0023, 0703 addendum 1) | `idf.py size` |
| 2026-11-24 | STARK-0703 | The 4 kB edge ring — heap held only during a measurement | Idle in the GPIO app 227 356 B, during a 4 s measurement 223 160 B (min 218 920 B), after leaving every app unchanged | `storage/v07-measure` |
| 2026-11-24 | STARK-0703 | FreeRTOS tasks added — **ADR-0023: none** | 0 (the measurement runs on the app's existing worker; the cancel is a volatile flag) | `app_gpio.c` |
| 2026-11-24 | STARK-0703 | Measurement arithmetic against the independent Python reference | 79 edge sets, every result equal to the reference exactly; the 49 measured ones within ±0.5 % (to 10 kHz) or ±2 % (to 50 kHz) in frequency and ±2 or ±5 points in duty with seeded jitter of up to ±2 µs; 10 000 seeded random edge sets never return a number outside their status. Arithmetic only: the signal on a pad is V1 | `test_lab_meas` |
| 2026-11-24 | STARK-0703 | Host line coverage: `lab_meas.c`, `lab_pins.c`, `lab_pwm.c`, `lab_transport_hal.c`, `fake_lab.c`, `gpio_views.c` | 100 % each | `test_host.sh --coverage` |
| 2026-11-24 | STARK-0703 | Host suite | 78 test programs (77 at STARK-0702): `test_lab_meas` 12 tests is new; `test_lab_pins` 34 → 45, `test_lab_service` 17 → 22, `test_fake_lab` 16 → 25, `test_hal` 18 → 22, `test_ui_gpio` 17 → 21 tests, under ASan and UBSan | `test_host.sh` |
| 2026-11-24 | STARK-0703 | Emulator wall time (docker, up to four at once) | `v07-measure-prod` 114 s; `storage/v07-measure` (about 15 s of virtual time) 168 s | `test_emu.sh` |
| 2026-11-24 | STARK-0703 | Root heap, first vs last `stark_diag` line over each 60 s soak — **gate ≤ 1 kB** | v01 241 976 → 242 012 B (Δ 36 B); v02 227 356 → 227 236 B (Δ 120 B); v03, v04, v05 227 356 → 227 260 B (Δ 96 B); v06 227 356 → 227 260 B (Δ 96 B), drops 0 in all. None of the soaks opens the GPIO app | `soak/v01…v06-soak` |
| 2026-11-24 | STARK-0703 | Scenarios, whole set: production image 23 (including `v07-measure-prod`), test image 30 (including `v07-measure`) | all pass; no pre-existing scenario was edited | `test_emu.sh` |
| 2026-11-24 | STARK-0704 | Production boot heap, first `diag: heap=` (STARK-0703 239 776 B) — **gate ≥ 200 kB, V0.7 total cost ≤ 16 kB; task budget ≤ 2 kB** | 238 228 B (−1 548 B; −2 504 B against the V0.6 close, 240 732 B, so 2.5 kB of the 16 kB so far). Static RAM grew by 1 492 B (`app_i2c` 1 364, `stark_lab` +128); the IRAM is still full, so the DIRAM overflow layout moves with the code and the figure includes that | `v0-boot`, `v07-i2c-prod` |
| 2026-11-24 | STARK-0704 | Test image boot heap (STARK-0703 234 808 B) | 232 700 B (−2 108 B: the app and service plus the test-only bus model's register files, about 0.6 kB, in `fake_lab.c`) | `storage/v07-i2c`, `storage/v07-i2c-bus` |
| 2026-11-24 | STARK-0704 | Firmware binary (STARK-0703 500 352 B) | 511 104 B (+10 752 B; the 52 kB ceiling of ADR-0023 §10 leaves 41 kB) | `build/stark-one.bin` |
| 2026-11-24 | STARK-0704 | Static RAM — **budget: `app_i2c` ≤ 2 kB, `stark_lab` I²C ≤ 1 kB**: `app_i2c` / `stark_lab` (613 B at STARK-0703) / `stark_lab_transport` (2 B) / `stark_hal` (30 B) | 1 364 B (1 360 `.bss`, 4 `.data`: the menu rows and labels, the scan result, the 256-byte dump and the editor) / 741 B (+128: the rules' state and the second lock) / 2 B / 30 B — +1 492 B | `idf.py size-components` |
| 2026-11-24 | STARK-0704 | Flash (code + rodata) — **budget ≤ 10 kB together, the hints ≤ 2 kB**: `app_i2c` / `stark_lab` (6 071 B at STARK-0703) / `stark_lab_transport` (765 B) | 5 012 B / 8 972 B (+2 901: `lab_i2c.c`, the hints and the service calls) / 1 334 B (+569: the I²C file) — 8 482 B; the hints table is 1 124 B (31 entries, 248 B of table and 876 B of text) | `idf.py size-components` |
| 2026-11-24 | STARK-0704 | IRAM | 16 384 of 16 384 B used (0 B free), unchanged; nothing of this task is in IRAM | `idf.py size` |
| 2026-11-24 | STARK-0704 | FreeRTOS tasks added — **ADR-0023: none** | 0 (the sweep, a read, a dump and a write run on the UI task, 112 probes of 5 ms at most; the I²C group has its own mutex) | `stark_lab.c` |
| 2026-11-24 | STARK-0704 | Heap held by the app — transient allocations | none: everything is static; each transaction attaches an I²C driver handle (a few hundred bytes) and removes it before it returns. Root heap idle in the app's launcher 225 248 B, after leaving the app 225 152 B (−96 B, the usual baseline) on the test image and 240 464 B unchanged on the production image | `storage/v07-i2c`, `v07-i2c-prod` |
| 2026-11-24 | STARK-0704 | The sweep, the refusals, the fingerprints and the hex text against the independent Python reference | 15 sweeps (list, error count, status and number of probes), 112 refusal cases (the core and the transport's guard agree with the reference on every one), 16 distinct fingerprints and 110 hex cases, every result equal to the reference. Rules only: a device on a bus is V1 | `test_lab_i2c` |
| 2026-11-24 | STARK-0704 | Host line coverage: `lab_i2c.c`, `lab_i2c_hints.c`, `i2c_views.c`, `fake_lab.c` (and the files of STARK-0701 to 0703) | 100 % each | `test_host.sh --coverage` |
| 2026-11-24 | STARK-0704 | Host suite | 81 test programs (78 at STARK-0703): `test_lab_i2c` 34, `test_i2c_views` 14 and `test_ui_i2c` 20 tests are new; `test_fake_lab` 25 → 31 tests, under ASan and UBSan; script unit tests 143 | `test_host.sh`, `check.sh` |
| 2026-11-24 | STARK-0704 | Emulator wall time (docker, three at once) | `v07-i2c-prod` 158 s (production image); `storage/v07-i2c` (13.6 s of virtual time) 180 s and `storage/v07-i2c-bus` (16.1 s) 186 s | `test_emu.sh` |
| 2026-11-24 | STARK-0704 | Root heap, first vs last `stark_diag` line over each 60 s soak — **gate ≤ 1 kB** | v01 240 428 → 240 464 B (Δ 36 B); v02 225 248 → 225 140 B (Δ 108 B); v03, v04, v05, v06 225 248 → 225 152 B (Δ 96 B), drops 0 in all. None of the soaks opens the I2C app | `soak/v01…v06-soak` |
| 2026-11-24 | STARK-0704 | Scenarios, whole set: production image 24 (including `v07-i2c-prod`), test image 32 (including `v07-i2c` and `v07-i2c-bus`), soaks v01 to v06 | all pass; the pre-existing scenarios edited are row indices, the registry line and one more press where a walk crosses the new launcher row (TASKS.md STARK-0704 lists each) | `test_emu.sh` |
| 2026-11-24 | STARK-0705 | Production boot heap, first `diag: heap=` (STARK-0704 238 228 B) — **gate ≥ 200 kB, V0.7 total cost ≤ 16 kB; task budget ≤ 3 kB** | 233 656 B (−4 572 B, over the 3 kB task budget; V0.7 total so far −7 076 B against the V0.6 close, 240 732 B). The UART driver's static state and the app's 1 388 B of `.bss` account for most of it | `v0-boot`, `v07-uart-prod` |
| 2026-11-24 | STARK-0705 | Test image boot heap (STARK-0704 232 700 B) | 226 752 B (−5 948 B: the production cost plus the test-only fake UART peer's scene buffers in `fake_lab.c`) | `storage/v07-uart` |
| 2026-11-24 | STARK-0705 | Firmware binary (STARK-0704 511 104 B) | 549 344 B (+38 240 B, over the 12 kB task budget and the 52 kB ceiling of ADR-0023 §10; ADR-0023 0705 point 13). `libesp_driver_uart.a` is 14 120 B of it, which no earlier task linked | `build/stark-one.bin` |
| 2026-11-24 | STARK-0705 | Static RAM — **budget `app_uart` ≤ 2 kB**: `app_uart` / `stark_lab` (613 B at STARK-0704) / `stark_lab_transport` (2 B) / `esp_driver_uart` | 1 388 B (all `.bss`) / 1 074 B (+461) / 8 B / 361 B (33 `.bss`, 328 `.data`) | `idf.py size-components` |
| 2026-11-24 | STARK-0705 | Flash (code + rodata) — **task budget ≤ 12 kB together**: `app_uart` / `stark_lab` (8 972 B at STARK-0704) / `stark_lab_transport` (765 B) / `esp_driver_uart` | 8 228 B / 14 781 B (+5 809: `lab_uart.c`, `lab_autobaud.c`, `lab_uart_svc.c` and the UART io table) / 2 251 B (+1 486) / 13 759 B | `idf.py size-components` |
| 2026-11-24 | STARK-0705 | IRAM | 16 384 of 16 384 B used (0 B free), unchanged; nothing of this task is in IRAM | `idf.py size` |
| 2026-11-24 | STARK-0705 | FreeRTOS tasks added — **ADR-0023: none** | 0 (the session runs on the app's existing worker; the cancel is a volatile flag; the driver's event queue is not a task) | `app_uart.c` |
| 2026-11-24 | STARK-0705 | Host line coverage: `lab_uart.c`, `lab_autobaud.c`, `lab_uart_svc.c`, `uart_views.c`, `uart_log.c`, `fake_lab.c` and the earlier lab files | 100 % each | `test_host.sh --coverage` |
| 2026-11-24 | STARK-0705 | Host suite | 87 test programs (81 at STARK-0704): `test_lab_uart`, `test_lab_autobaud`, `test_lab_uart_svc`, `test_uart_views`, `test_uart_log` and `test_ui_uart` (37 pinned frames) are new; `test_fake_lab` gained the UART peer tests; the independent Python reference adds 60 wire-time, 14 ring, 21 loop, 29 row, 41 file, 27 name, 7 CRC-32 and 72 auto-baud vectors; script unit tests 161, under ASan and UBSan | `test_host.sh`, `check.sh` |
| 2026-11-24 | STARK-0705 | Emulator wall time (docker, three at once, host load average 30 to 40 from other projects) | `v07-uart-prod` 247 s; `storage/v07-uart` 258 s, `v07-uart-bauds` 299 s, `v07-uart-formats` 141 s, `v07-uart-log` 203 s | `test_emu.sh` |
| 2026-11-24 | STARK-0705 | Root heap, first vs last `stark_diag` line over each 60 s soak — **gate ≤ 1 kB** | v03 219 300 → 219 204 B (Δ 96 B, PASS, 1 447 s wall); v06 Δ 96 B (PASS, 1 338 s wall). v01, v02, v04 and v05 were wall-capped by the emulator's own limit under host load; up to the cap v01 235 856 → 235 892 B (Δ 36 B), v02 Δ 108 B, v04 Δ 96 B, v05 Δ 96 B. A wall cap is not a firmware failure; the CI soak job is the authoritative run | `soak/v01…v06-soak` |
| 2026-11-24 | STARK-0705 | Event drops over the soaks — **gate 0** | 0 in every soak log, capped or not | `test_emu.py` drops gate |
| 2026-11-24 | STARK-0705 | Scenarios, whole set: production image 25 (including `v07-uart-prod`), test image 36 (including `v07-uart`, `v07-uart-bauds`, `v07-uart-formats`, `v07-uart-log`) | all pass; the pre-existing scenarios edited are the registry line, row indices and extra presses where a walk crosses the new launcher row (TASKS.md STARK-0705 lists each) | `test_emu.sh` |
| 2026-11-24 | STARK-0706 | Production boot heap, first `diag: heap=` (STARK-0705 233 656 B) — **gate ≥ 200 kB, V0.7 total cost ≤ 16 kB; task budget ≤ 1 kB** | 233 292 B (−364 B; −7 440 B against the V0.6 close, 240 732 B, so 7.4 kB of the 16 kB is spent). The app's menu rows, editor state and answer are 0.9 kB of heap held only while the app runs (on_start allocates, on_stop frees), so they cost nothing here | `v0-boot`, `v07-spi-prod` |
| 2026-11-24 | STARK-0706 | Test image boot heap (STARK-0705 226 752 B) | 226 356 B (−396 B: the production cost plus the test-only SPI model in `fake_lab.c`) | `storage/v07-spi` |
| 2026-11-24 | STARK-0706 | Firmware binary (STARK-0705 549 344 B) | 556 784 B (+7 440 B; the V0.7 ceiling of ADR-0023 §10 was passed at STARK-0705 and is not moved here) | `build/stark-one.bin` |
| 2026-11-24 | STARK-0706 | Static RAM — **budget: `app_spi` ≤ 1 kB**: `app_spi` / `stark_lab` (1 074 B at STARK-0705) / `stark_lab_transport` (8 B) | 170 B (`.bss`: the menu and a few indices) / 1 214 B (+140: the probe's state and mutex) / 8 B (±0) | `idf.py size-components` |
| 2026-11-24 | STARK-0706 | Flash (code + rodata) — **task budget ≤ 6 kB together**: `app_spi` / `stark_lab` (14 781 B at STARK-0705) / `stark_lab_transport` (2 251 B) | 3 870 B / 17 061 B (+2 280: `lab_spi.c` and the service calls) / 2 571 B (+320: `lab_transport_spi.c`) = +6 470 B, 326 B over the 6 kB budget (the state, reason texts and log lines of the probe, the app's rows and the 49-row answer view); flagged, not moved | `idf.py size-components` |
| 2026-11-24 | STARK-0706 | IRAM | 16 384 of 16 384 B used (0 B free), unchanged; nothing of this task is in IRAM | `idf.py size` |
| 2026-11-24 | STARK-0706 | FreeRTOS tasks added — **ADR-0023: none** | 0 (a transaction runs on the UI task, 32 bytes at 100 kHz is 2.6 ms; the probe has its own mutex) | `stark_lab.c` |
| 2026-11-24 | STARK-0706 | Judgement, fingerprints and device answers against the independent Python reference | 220 judgement vectors (every state against 22 declared-bit mixes, the chip select wired or not, agreeing with the transport's predicate), 14 fingerprints (all distinct) and 54 device answers, every one equal | `test_lab_spi`, `test_fake_lab` |
| 2026-11-24 | STARK-0706 | Host line coverage: `lab_spi.c`, `spi_views.c`, `fake_lab.c`, `stark_lab_transport.h` (and the earlier lab files) | 100 % each | `test_host.sh --coverage` |
| 2026-11-24 | STARK-0706 | Host suite | 90 test programs (87 at STARK-0705): `test_lab_spi` 37, `test_spi_views` 11 and `test_ui_spi` 13 tests (16 pinned frames) are new; `test_fake_lab` 41 → 46, `test_module_desc` 5 → 6 tests, under ASan and UBSan; script unit tests 168 | `test_host.sh`, `check.sh` |
| 2026-11-24 | STARK-0706 | Emulator wall time (docker, two at once, host load average 20 to 30 from other projects) | `v07-spi-prod` 71 s; `storage/v07-spi` (19.2 s of virtual time) 275 s | `test_emu.sh` |
| 2026-11-24 | STARK-0706 | Root heap, first vs last `stark_diag` line over each 60 s soak — **gate ≤ 1 kB** | v01 235 492 → 235 528 B (Δ 36 B, 1 331 s wall); v02 218 904 → 218 796 B (Δ 108 B); v03, v04, v05, v06 218 904 → 218 808 B (Δ 96 B). None was wall-capped; drops 0 in all | `soak/v01…v06-soak` |
| 2026-11-24 | STARK-0706 | Scenarios, whole set: production image 26 (including `v07-spi-prod`), test image 37 (including `storage/v07-spi`) | all pass; the pre-existing scenarios edited are the registry line, row indices and extra presses where a walk crosses the new launcher row (TASKS.md STARK-0706 lists each) | `test_emu.sh` |
| 2026-11-24 | STARK-0707 | Production boot heap, first `diag: heap=` (STARK-0706 233 292 B) — **gate ≥ 200 kB, V0.7 total cost ≤ 16 kB; task budget ≤ 2.5 kB** | 231 952 B (−1 340 B; −8 780 B against the V0.6 close, 240 732 B, so 8.8 kB of the 16 kB is spent). The checks keep their scratch on the heap only while one runs and the app's 1.2 kB of menu state only while it is open, so what the boot pays is static RAM and the IRAM-overflow layout (the 16 kB IRAM is still full) | `v0-boot`, `v07-selftest-prod` |
| 2026-11-24 | STARK-0707 | Test image boot heap (STARK-0706 226 356 B) | 225 032 B (−1 324 B) | `storage/v07-selftest` |
| 2026-11-24 | STARK-0707 | Firmware binary (STARK-0706 556 784 B) | 572 016 B (+15 232 B; the V0.7 ceiling of ADR-0023 §10 was passed at STARK-0705 and STARK-0708 restates it) | `build/stark-one.bin` |
| 2026-11-24 | STARK-0707 | Static RAM — **budget: `app_selftest` + core ≤ 2.5 kB**: `app_selftest` / `stark_diag` (the runner's instance with its 12-row table, 853 B, and the mutex, about 80 B) / `main` (the checks' environment table, `.data`) | 233 B / about 933 B of the library's 1 605 B / 124 B — about 1.3 kB | `idf.py size-components`, `nm` |
| 2026-11-24 | STARK-0707 | Flash (code + rodata) — **budget ≤ 8 kB**: `app_selftest` / the pure self-test files of `stark_diag` (`selftest_core` 1 211 B, `selftest_report` 1 886, `selftest_svc` 610) / `main` (`selftest_checks` 4 628 B, `selftest_sha256` 1 210, `selftest_export` 931, `selftest_target` 667) / `stark_log` and `stark_settings` (+494 and about +120) | 3 937 B (3 761 code, 176 rodata) / 3 707 B / 7 436 B / about 0.6 kB — **15.2 kB in the image, 7.2 kB over the budget** (the objects are `-Og` figures). The bulk is the checks' evidence texts and step logic, the image-header walk, the SHA-256 and the report formatter; the budget did not count the walk screen or the export's read-back | `idf.py size-components`, `xtensa-esp32s3-elf-size` |
| 2026-11-24 | STARK-0707 | IRAM | 16 384 of 16 384 B used (0 B free), unchanged; nothing of this task is in IRAM | `idf.py size` |
| 2026-11-24 | STARK-0707 | FreeRTOS tasks added — **ADR-0023: none** | 0 (a check runs on the app worker, the export too; the runner is called, it has no task of its own) | `selftest_svc.c` |
| 2026-11-24 | STARK-0707 | Transient heap — held only while it is used | The app's menu state 1 220 B while the app is open (production first `diag:` line 234 188 B before opening, 232 968 B inside it, 234 188 B after leaving: Δ 0 B); the RAM-check block 32 768 B (a quarter of the largest free block is more than 32 kB here) with the lowest free heap ever seen 200 196 B on the production image and 183 324 B on the test image, which also writes the report: never near the 128 kB floor; the report's 5.5 kB (snapshot, tasks, module lines, rows, the 4 kB log-tail window) | `v07-selftest-prod`, `storage/v07-selftest` |
| 2026-11-24 | STARK-0707 | Least free stack of any task when the events check ran (the check demands 256 B) | 568 B (12 tasks) on the production image, 568 B (13 tasks) on the test image | `selftest: events and stacks pass …` |
| 2026-11-24 | STARK-0707 | Reports written by the test image | `report_0001.txt` and `report_0002.txt`, 1 447 B each (limit 16 384 B): the log tail is only what the storage service has not yet drained, about a second of lines | `storage/v07-selftest` |
| 2026-11-24 | STARK-0707 | SHA-256 over the running image, in the emulator | 572 016 B image (`image 572016 B` in the log: the whole `.bin`, header to appended digest), 2 read-only passes that must agree (the first 4 bytes of the digest are in the evidence) | `v07-selftest-prod` |
| 2026-11-24 | STARK-0707 | Host line coverage: `selftest_core.c`, `selftest_report.c`, `selftest_svc.c`, `selftest_checks.c`, `selftest_export.c`, `selftest_sha256.c`, `selftest_views.c`, `log_ring.c` (and the earlier lab files) | 100 % each | `test_host.sh --coverage` |
| 2026-11-24 | STARK-0707 | Host suite | 96 test programs (90 at STARK-0706): `test_selftest_core` 14, `test_selftest_report` 10, `test_selftest_checks` 38, `test_selftest_export` 14, `test_selftest_views` 18 and `test_ui_selftest` 18 tests (16 pinned frames) are new; `test_log_ring` 5 → 8 tests, under ASan and UBSan; script unit tests 168 | `test_host.sh`, `check.sh` (25 checks, `selftest-flash-readonly` is new) |
| 2026-11-24 | STARK-0707 | Emulator wall time (docker, two at once, host load average 20 to 30 from other projects) | `v07-selftest-prod` 217 s; `storage/v07-selftest` (12.5 s of virtual time) 215 s and `storage/v07-selftest-faults` (13 s) 171 s | `test_emu.sh` |
| 2026-11-24 | STARK-0707 | Root heap, first vs last `stark_diag` line over each 60 s soak — **gate ≤ 1 kB** | v01 234 152 → 234 188 B (Δ 36 B, 1 184 s wall); v02 217 580 → 217 472 B (Δ 108 B, 1 433 s); v03, v04, v05, v06 217 580 → 217 484 B (Δ 96 B). None was wall-capped; drops 0 in all. None of the soaks opens the Self-Test app | `soak/v01…v06-soak` |
| 2026-11-24 | STARK-0707 | Scenarios, whole set: production image 27 (including `v07-selftest-prod`), test image 39 (including `storage/v07-selftest` and `storage/v07-selftest-faults`) | all pass. In the full test-image run `storage/v06-detect` hit the harness's 600 s wall-clock cap at 30 s of its 54 s of virtual time (host load average 30) and passed alone with the cap raised to 3 000 s (473 s), so it is recorded as wall-capped under load, not a failure. The pre-existing scenarios edited are the registry line, row indices and extra presses where a walk crosses the new launcher row (TASKS.md STARK-0707 lists each) | `test_emu.sh` |

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
| CC1101 identified over SPI at 4 MHz; `CHIP_RDYn` and reset timing (V0.6) | pass / (record) | | |
| RSSI of our own transmitter at 0.5, 1, 2, 4, 8 m; against a known level (V0.6) | falls with distance / offset within ±6 dB | | |
| Scan peak at our own transmitter's frequency (V0.6) | within 25 kHz | | |
| Capture of our own remote against its documented timing; decode (V0.6) | ±10 % / matches | | |
| Replay of our own capture triggers our own receiver (V0.6) | pass | | |
| Conducted power at `PATABLE` 0x60 (V0.6) | ≤ +3 dBm | | |
| On-air time and duty over an hour of permitted sends (V0.6) | ≤ 360 s, off-time ≥ 9× | | |
| GDO0 waveform vs the file; carrier gone after the last symbol (V0.6) | ≤ 2 µs error, ends low | | |

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
