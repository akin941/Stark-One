# HARDWARE — STARK ONE

Scope: V0 (simulated + breadboard) through V1 (custom PCB). Pin assignments here are
normative — `components/stark_board` is the only place they may be encoded in firmware.

---

## 1. Target selection

| Stage | Hardware |
| --- | --- |
| V0 (simulation) | Wokwi `board-esp32-s3-devkitc-1` configured as the N16R8 SKU: flash 16 MB, PSRAM 8 MB octal — with PSRAM **unused by firmware** (ADR-0014) |
| V0.x / V1 prototype | ESP32-S3-DevKitC-1 (N16R8 variant preferred) on breadboard |
| V1 hardware | ESP32-S3-WROOM-1-N16R8 module on custom 4-layer PCB |

**Why ESP32-S3** (full rationale in ADR-0001): dual-core 240 MHz, 512 kB SRAM, native
USB (CDC + DFU without a UART bridge), plenty of GPIO after the octal-PSRAM reservation,
excellent `esp_lcd` DMA path for SPI displays, first-class Wokwi simulation, and a
single vendor toolchain for the whole product life.

**Critical module constraint (N16R8):** the WROOM-1 N16R8 uses **octal** PSRAM. GPIO
**26–37 are consumed internally** by the flash/PSRAM interface and are not available on
the module pads. Every pin assignment below avoids that range, so the DevKitC-1
breadboard map and the future PCB map are identical.

Verified against Espressif's ESP32-S3 hardware design guidelines and ESP-IDF's own
`soc/spi_pins.h`: the functionally-required minimum is **GPIO 27–37** — the baseline
flash SPI0/1 IOMUX pins (27–32) plus octal PSRAM's extra DQ4–7/DQS lines (33–37). GPIO
26 is not itself wired to the flash/PSRAM interface. It is included in the reserved
range anyway as a one-pin safety margin, not a functional requirement — no signal in
this pin map needs it, so excluding it costs nothing and reduces the chance of a
mis-remembered "27" becoming a real bug later.

Additionally reserved:

| Pins | Reason |
| --- | --- |
| GPIO 19, 20 | USB D− / D+ |
| GPIO 43, 44 | UART0 TX/RX — the console (Wokwi serial monitor reads this) |
| GPIO 0, 3, 45, 46 | Strapping pins (boot mode, JTAG source, VDD_SPI, ROM msg) |
| GPIO 26–37 | Internal SPI flash + octal PSRAM |

That leaves GPIO 1–18, 21, 38–42, 47, 48 — 25 usable pins, which the map below spends
deliberately.

---

## 2. V0 pin map (normative)

### 2.1 Active in V0

| Signal | GPIO | Direction | Notes |
| --- | --- | --- | --- |
| `TFT_SCLK` | 12 | OUT | SPI2 (FSPI) bus clock |
| `TFT_MOSI` | 11 | OUT | SPI2 MOSI |
| `TFT_MISO` | 13 | IN | SPI2 MISO — unused by the panel, reserved for SD |
| `TFT_CS` | 10 | OUT | Panel chip select |
| `TFT_DC` | 9 | OUT | Data/command |
| `TFT_RST` | 14 | OUT | Hardware reset — **not modelled by Wokwi**, still driven |
| `TFT_BL` | 21 | OUT | Backlight, LEDC PWM — **not modelled by Wokwi** |
| `BTN_UP` | 4 | IN pull-up | Active low |
| `BTN_DOWN` | 5 | IN pull-up | Active low |
| `BTN_LEFT` | 6 | IN pull-up | Active low |
| `BTN_RIGHT` | 7 | IN pull-up | Active low |
| `BTN_OK` | 15 | IN pull-up | Active low |
| `BTN_BACK` | 16 | IN pull-up | Active low |
| `BUZZER` | 17 | OUT | Passive piezo via LEDC, 2–5 kHz |
| `LED_STATUS` | 18 | OUT | Discrete LED + 330 Ω to GND, active high |

### 2.2 Reserved for later milestones (do not reuse)

| Signal | GPIO | Milestone | Notes |
| --- | --- | --- | --- |
| `SD_CS` | 8 | V0.2 | microSD shares SPI2 with the panel |
| `I2C_SDA` | 1 | V0.4 | Shared I²C: NFC, module ID EEPROM, sensors, lab I²C |
| `I2C_SCL` | 2 | V0.4 | |
| `IR_TX` | 47 | V0.3 | RMT TX → IR LED driver |
| `IR_RX` | 48 | V0.3 | RMT RX ← 38 kHz demodulator (TSOP-class) |
| `SUBGHZ_CS` | 40 | V0.6 | CC1101-class module on SPI2 |
| `SUBGHZ_GDO0` | 41 | V0.6 | |
| `SUBGHZ_GDO2` | 42 | V0.6 | |
| `LAB_UART_TX` | 38 | V0.7 | UART1, level-limited, series-protected |
| `LAB_UART_RX` | 39 | V0.7 | |

**DevKitC-1 caveat:** the on-board addressable RGB LED sits on **GPIO 48** (board rev
v1.0) or **GPIO 38** (rev v1.1). Both are only used from V0.3/V0.7 onward, and in both
cases the conflict is cosmetic (stray LED flicker) rather than functional. On the
custom PCB the conflict disappears. V0 uses neither pin — `LED_STATUS` is a discrete
LED on GPIO 18 so the behaviour is identical in Wokwi and on hardware.

### 2.3 SPI bus policy

One bus, `SPI2_HOST`, shared by panel / SD / Sub-GHz with independent CS lines.

* `stark_board_init()` calls `spi_bus_initialize(SPI2_HOST, …)` once; devices attach
  afterwards.
* Max transfer size sized for one band: `320 × BAND_H × 2` bytes (default 25 600).
* Clock: 40 MHz for the panel on PCB; **20 MHz on breadboard** — long jumper wires
  will not survive 40 MHz. Kconfig `STARK_DISPLAY_SPI_HZ`.
* SD runs at 20 MHz max and re-negotiates per transaction (the ESP-IDF SPI driver
  handles per-device clocks).
* Sub-GHz (CC1101) tops out at ~6.5 MHz; per-device clock again.

Mixing a display and an SD card on one bus is the known-awkward part: an SD access
during a panel DMA transfer must not interleave. The driver serialises through the
`stark_display` owner (ARCHITECTURE §8) — SD I/O happens from the UI task, or from a
worker that acquires the bus through the same API.

---

## 3. V0 bill of materials (breadboard)

| # | Item | Qty | Note |
| --- | --- | --- | --- |
| 1 | ESP32-S3-DevKitC-1-N16R8 | 1 | USB-C, on-board USB-Serial-JTAG |
| 2 | 2.8" ILI9341 SPI TFT, 320×240 | 1 | Non-touch variant is sufficient; 3.3 V logic |
| 3 | Tactile push buttons 6×6 mm | 6 | To GND, internal pull-ups |
| 4 | Passive piezo buzzer | 1 | Driven by LEDC; series 100 Ω optional |
| 5 | 5 mm LED + 330 Ω | 1 | Status LED |
| 6 | 100 nF decoupling caps | 2–3 | Across panel and buzzer supply |
| 7 | Breadboard + jumpers | – | Keep SPI wires < 10 cm |

Later milestones add: microSD breakout (V0.2), TSOP38238 + IR LED + NPN driver (V0.3),
PN532 or ST25R-class NFC module (V0.4), CC1101 433 MHz module + SMA antenna (V0.6),
Li-ion cell + TP4056-class charger + gauge (V1).

### 3.1 Power notes (breadboard)

* Panel backlight draws 60–100 mA — feed it from the DevKit's 3V3 only if the USB
  supply is solid; otherwise a separate LDO. Backlight PWM helps.
* ESP32-S3 Wi-Fi TX peaks ~350 mA; Wi-Fi is off in V0, but the PCB regulator must be
  sized for it regardless (≥ 1 A).
* Total V1 budget estimate: 120 mA idle-with-backlight, 250 mA active, < 1 mA deep
  sleep (V2 work). A 2000 mAh cell targets ≥ 8 h of active use.

---

## 4. Display details

| Property | Value |
| --- | --- |
| Controller | ILI9341 |
| Resolution | 320 × 240, landscape (rotation 1) |
| Colour | RGB565, 16-bit |
| Interface | 4-wire SPI (SCLK/MOSI/CS/DC) + RST + backlight |
| Driver | `espressif/esp_lcd_ili9341` pinned `==2.1.0` (fallback `==2.0.2`, ADR-0004) on top of `esp_lcd_panel_io_spi` |
| Band buffer | 320 × 40 × 2 B = 25 600 B, ×2 for ping-pong DMA |
| Expected full-screen refresh | ~25 ms at 40 MHz SPI (theoretical 153.6 kB ≈ 31 ms wire time) |

Rotation, colour inversion and BGR order differ between ILI9341 panel batches. The
driver exposes Kconfig for `swap_xy` / `mirror_x` / `mirror_y` / `bgr` so a wrong-looking
panel is a config change, not a code change. Wokwi's part uses the canonical
configuration, so the defaults must match Wokwi, with a documented note that a physical
panel may need adjustment at bring-up.

---

## 5. Input details

Six momentary switches to GND, internal pull-ups enabled, sampled at 5 ms (no
interrupts, no external RC). Debounce 20 ms in software (ARCHITECTURE §6.6).

Physical layout (V1 enclosure): D-pad diamond (UP/LEFT/RIGHT/DOWN) with OK at the
centre and BACK to the lower-right of the cluster. In V0 the Wokwi diagram mirrors this
arrangement visually so muscle memory transfers.

Future: a `BOOT` button (GPIO 0) exists on the DevKit and is *not* used as a UI key —
holding it at reset must keep its firmware-download meaning.

---

## 6. Expansion / module bus (V0.5 target, defined here for pin discipline)

A single 12-pin connector carrying:

| Pin | Signal | Note |
| --- | --- | --- |
| 1, 2 | 3V3, GND | 300 mA budget per module |
| 3, 4 | I²C SDA/SCL | Module identification EEPROM lives here |
| 5–7 | SPI SCLK/MOSI/MISO | Shared SPI2 |
| 8 | MOD_CS | Per-slot chip select |
| 9, 10 | MOD_IRQ0/1 | GPIO interrupts (e.g. CC1101 GDO0/GDO2) |
| 11 | MOD_EN | Module power enable / reset |
| 12 | VBAT | Raw battery for modules needing > 3V3 headroom |

Identification: a 24C02-class EEPROM at a fixed I²C address per slot holding a small
descriptor (module ID, revision, capability bits). `stark_module` reads it at boot and
publishes capabilities; apps enable themselves accordingly. This is what makes "plug in
a Sub-GHz module and the radio apps light up" work without firmware edits.

---

## 7. RF / regulatory notes

* Sub-GHz work targets the **433.05–434.79 MHz** SRD band. In Turkey and the EU this
  band carries duty-cycle and ERP limits (commonly ≤ 10 mW ERP and ≤ 10 % duty cycle
  for the general SRD sub-band). Transmit features must be rate-limited in firmware and
  must default to **receive-only**.
* IR emission is unregulated but line-of-sight; IR TX is limited to our own devices.
* NFC operates at 13.56 MHz with a compliant reader module; no custom antenna design
  before V1 hardware.
* Antenna choice, matching and any conducted/radiated testing are V1-hardware concerns.
  Until then all RF modules are off-the-shelf certified modules.

See [docs/SECURITY_SCOPE.md](docs/SECURITY_SCOPE.md) for the *use* policy that sits on
top of these technical limits.

---

## 8. Path to the custom PCB (V1 hardware)

1. Freeze the V0.7 pin map (this document) — no further pin churn.
2. KiCad schematic: MCU module, USB-C + ESD + CC resistors, 3V3 regulator, Li-ion
   charger + protection + fuel gauge, panel FPC connector, button matrix (direct GPIO),
   buzzer driver, microSD, IR front-end, module connector, test points.
3. Design rules: 4 layers (SIG/GND/PWR/SIG), 50 Ω single-ended for USB, keep SPI < 50 mm,
   ground pour under the panel flex, no traces under the module antenna keep-out.
4. DFM: JLCPCB-class 4-layer, 0.2 mm/0.2 mm, assembled where practical.
5. Bring-up order: power rails → USB enumeration → flash/boot → display → buttons →
   SD → radios. Each step is a line item in the hardware test checklist ([TESTING.md]).

## 9. Enclosure (V1 enclosure)

Two-part clamshell, M2 heat-set inserts, silicone or PCB-mounted tactile keycaps,
light-pipe for the status LED, microSD and USB-C access cutouts, module bay on the top
edge. First iteration is FDM-printed for fit, second is resin for finish. Mechanical
constraints (board outline, connector positions, button heights) must be exported from
KiCad *before* the enclosure work starts.
