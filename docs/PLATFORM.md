# PLATFORM — the port seam and the capability matrix

**Scope:** this document is the inventory [ADR-0025](../DECISIONS.md) calls for: where the
hardware seam is today, which code is behind it (the ports) and which is in front of it (the
pure cores), and the capability matrix that decides, per board, which features a build offers.
It describes STARK ONE **as built** — one firmware, one port (ESP32-S3). It adds no code and
changes no behaviour; it is the map a second port (STARK-0903, a later milestone) would be
designed against.

Two live checks keep this document honest, so it never drifts into a hand-maintained list that
rots:

* **`scripts/check_layers.py`** enforces the component layer rules (ARCHITECTURE §2, §10): a
  *pure* component may require no platform (ESP-IDF) component, and every `stark_*` component
  must be classified. A core that reaches for ESP-IDF fails the lint job.
* **The host test build** (`test/host`, `scripts/test_host.sh`) compiles the pure cores — and
  only the pure cores — with no ESP-IDF in the include path. A file that needs ESP-IDF cannot
  be host-compiled, so the split below is not an opinion: it is what each build can and cannot
  do.

---

## 1. The seam

The split is **per file**, not per component: a component such as `stark_ir` holds pure codec
cores (`ir_nec.c`, `ir_rc5.c`, …) *and* its ESP-IDF port (`stark_ir.c`) side by side.

### 1.1 Ports — the files that bind ESP-IDF

These include an ESP-IDF header directly (`driver/*`, `esp_*`, `freertos/*`, `nvs`, `sdmmc`,
…). They are the entire surface a new architecture would have to re-provide. Verifiable with:

```bash
grep -rlE '#include[ ]+[<"](driver/|esp_|freertos/|hal/|soc/|nvs|sdmmc|esp_private|sdkconfig)' \
  components/ apps/ main/ --include='*.c'
```

| Concern | Port file(s) |
| --- | --- |
| Board pin map, clocks, `board_init`, status LED | `stark_board/board_devkitc1.c`, `stark_board/stark_led.c` |
| HAL: time, task, GPIO, SPI, I²C, PWM | `stark_hal/stark_hal_esp.c` |
| Event loop (FreeRTOS primitives) | `stark_event/stark_event.c` |
| Logging sink | `stark_log/stark_log.c` |
| Display panel (SPI driver) | `stark_display/display_panel.c` |
| Input service (GPIO/timer) | `stark_input/input_service.c` |
| Buzzer (LEDC/RMT) | `stark_buzzer/buzzer.c` |
| Diagnostics (heap, timing) | `stark_diag/stark_diag.c` |
| App worker task | `stark_app/app_worker.c` |
| UI stack timing | `stark_ui/ui_stack.c` |
| Storage task + SD medium | `stark_storage/stark_storage.c`, `stark_storage_medium/medium_sdspi.c` |
| Settings over NVS | `stark_settings/stark_settings.c` |
| IR RMT RX/TX port | `stark_ir/stark_ir.c` |
| NFC task + I²C transport | `stark_nfc/stark_nfc.c`, `stark_nfc_transport/nfc_transport_i2c.c` |
| Module bus: enumeration, SPI/CS arbitration, I²C ID EEPROM | `stark_module/stark_module.c`, `stark_module/stark_module_spi.c`, `stark_module_transport/module_transport_i2c.c` |
| Sub-GHz service + hardware transport | `stark_subghz/stark_subghz.c`, `stark_subghz_transport/subghz_transport_hw.c` |
| Lab tools: service + I²C/UART transports | `stark_lab/stark_lab.c`, `stark_lab_transport/lab_transport_i2c.c`, `stark_lab_transport/lab_transport_uart.c` |
| Composition root, settings appliers, target self-test | `main/stark_main.c`, `main/settings_apply.c`, `main/selftest_target.c` |
| About / Hello apps (firmware description) | `apps/app_about/app_about.c`, `apps/app_hello/app_hello.c` |

### 1.2 Pure cores — in front of the seam

Everything the host build compiles: the protocol codecs (IR NEC/RC5/RC6/SIRC, NDEF, PN532
framing, CC1101 register logic, Manchester/OOK decoders), the UI model, `stark_gfx`,
`stark_input_core`, the event model, `stark_err`, and each service's pure logic that receives
its hardware by **injected transport** (`test/fake/*` in tests, the real port on the device).
These include no ESP-IDF header and are tested on the host under ASan/UBSan. The seam is the
*transport* interface each core is handed — this is exactly how a second port would slot in:
keep the cores, write a new transport + board package.

---

## 2. The capability surface

A feature's presence is decided at **run time**, never by a build flag (ADR-0010, ADR-0025).
Two mechanisms carry it:

**(a) Board pin presence — `stark_board` (L0).** The active board's pin map (`board_devkitc1.c`,
selected by Kconfig) sets a line to `-1` when the board does not wire it; a service degrades
rather than failing the boot (ARCHITECTURE §6.1). The gating lines are `sd_cs`, `i2c_sda/scl`,
`ir_tx/rx`, `lab_uart_tx/rx`, and each module slot's `cs`/`irq`.

**(b) Runtime capability bits — `components/stark_module/include/stark_caps.h` (ADR-0009/0021).**
A bit is **declared** by a module descriptor (I²C ID EEPROM) and becomes **effective only once a
driver confirms it**. An app names what it needs in `caps_required`; `stark_app_set_caps()` dims a
row whose bits are missing and `stark_app_launch()` refuses it.

| Bit | Meaning |
| --- | --- |
| `STARK_CAP_IR_TX` | onboard IR LED driver |
| `STARK_CAP_IR_RX` | onboard IR demodulator |
| `STARK_CAP_NFC` | a PN532-class reader answers on the shared I²C bus |
| `STARK_CAP_MOD_SPI` | slot module uses `MOD_CS` on shared SPI2 |
| `STARK_CAP_MOD_IRQ` | slot module uses `MOD_IRQ0/1` |
| `STARK_CAP_SUBGHZ` | a CC1101-class radio answers on a claimed slot |

---

## 3. The capability matrix — one board today

The 18 registered apps, and what each needs. Only three are capability-gated; the rest require
nothing and are present on any board with a panel and keys. For the one board that exists
(ESP32-S3-DevKitC-1), the right column is the bare breadboard ("bench E", no card, no modules);
fitting the part named in `caps_required` turns the row on.

| App (`id`) | Category | `caps_required` | On the bare board |
| --- | --- | --- | --- |
| `about`, `diagnostics`, `selftest` | System | 0 | present |
| `settings`, `modules` | Device | 0 | present |
| `storage` | Device | 0 | present (mounts a card only when `sd_cs` is wired and a card is in) |
| `hello` | Examples | 0 | present |
| `displaytest`, `inputtest`, `buzzertest`, `apptest` | Tests | 0 | present |
| `gpio`, `i2c`, `uart` | Lab | 0 | present (lab pins always offered) |
| `spi` | Lab | 0 | present; a probe needs an **empty** module slot |
| `ir` | Lab | `STARK_CAP_IR_TX \| STARK_CAP_IR_RX` | **absent** until the IR driver + receiver are fitted and confirmed |
| `nfc` | Lab | `STARK_CAP_NFC` | **absent** until a PN532 answers on I²C |
| `subghz` | Lab | `STARK_CAP_SUBGHZ` | **absent** until a CC1101 module is in a claimed slot |

This is the shape ADR-0025's new feature families (Wi-Fi/BLE analysis, 125 kHz RFID + 1-Wire,
USB-HID) will join: each adds a capability bit (or a board flag, for onboard radio such as
Wi-Fi/BLE), a pure core behind a transport seam, and one gated app — present on a build whose
board declares the capability, dimmed and unlaunchable on one that does not. No feature is ever
compiled out of the shared logic.

A machine-checked version of this matrix, and the guard that no per-feature or per-target
`#ifdef` creeps into the cores, is STARK-0902 (`scripts/check_caps.py`). A written analysis of
what a second architecture (STM32 / RP2350 / Linux) would satisfy or lack behind this seam is
STARK-0903.
