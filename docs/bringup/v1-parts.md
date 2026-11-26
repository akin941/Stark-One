# V1 prototype — parts and instruments

One buying and borrowing list for the V1 breadboard prototype (ROADMAP "V1 prototype"),
collected from HARDWARE.md §3 and the five bring-up procedures in this folder. Written by
STARK-0802; the prototype values in §3 are decided in ADR-0024. **Nothing here has been
bought, wired or measured.**

Every row names the procedure that needs it, so a part can be skipped if its procedure is
not being run yet. Part numbers are examples of a class, not a sourcing requirement: any part
that meets the stated rule will do, and the one actually used is written into the procedure's
setup table (each has a `________` for it).

**Scope (SECURITY_SCOPE §1, §2).** Every module, tag, remote, transmitter and receiver is
ours. **Logic is 3.3 V only**: nothing on this list may put more than 3.3 V (3.6 V absolute
ceiling) on a GPIO, the I²C bus or the SPI bus. The only 5 V node on the breadboard is the IR
LED's supply (§3.1), which never reaches a GPIO.

Procedures: **core** = [v1-core.md](v1-core.md), ROADMAP V1 exits 2–8 (display, input, buzzer, LED, storage);
**IR** = [v1-ir.md](v1-ir.md); **NFC** = [v1-nfc.md](v1-nfc.md); **MOD** =
[v1-modules.md](v1-modules.md); **RF** = [v1-subghz.md](v1-subghz.md); **LAB** =
[v1-labtools.md](v1-labtools.md).

## 1. Parts

### 1.1 Core (needed by every procedure)

| # | Part | Qty | Rule | Needed by |
| --- | --- | --- | --- | --- |
| 1 | ESP32-S3-DevKitC-1-N16R8 | 1 | 16 MB flash, 8 MB octal PSRAM; record the board revision (v1.0: RGB LED on GPIO 48, v1.1: on GPIO 38, HARDWARE §2.2) | all |
| 2 | 2.8" ILI9341 SPI TFT, 320×240 | 1 | 3.3 V logic, non-touch is enough; record whether its own SD socket is populated (not used: the card has its own breakout) | core, all |
| 3 | Tactile switch 6×6 mm | 7 | Six keys to GND (internal pull-ups) and one spare for the LAB bounce check | core, LAB |
| 4 | Passive piezo buzzer | 1 | Passive (no built-in oscillator); optional 100 Ω series | core |
| 5 | 5 mm LED + 330 Ω | 1 + 1 | Status LED on GPIO 18, active high | core |
| 6 | 100 nF ceramic capacitors | 5 | Panel, buzzer, card breakout, IR receiver, module carrier supply | core, IR, MOD |
| 7 | 10 µF electrolytic or ceramic | 2 | Panel and card supply bulk | core |
| 8 | microSD breakout | 1 | See §3.3: 3.3 V direct-connect, MISO released when deselected | core, LAB |
| 9 | microSD card | 2 | FAT32, ours, holding nothing else; plus one card formatted non-FAT for the "non-FAT card" gate (V1 exit 8) | core, LAB |
| 10 | Breadboards + jumpers | 2 boards | SPI jumpers under 10 cm (HARDWARE §2.3, 20 MHz on a breadboard) | all |
| 11 | USB-C data cable | 1 | Data-capable, for flashing and the console | all |

### 1.2 IR front-end

| # | Part | Qty | Rule | Needed by |
| --- | --- | --- | --- | --- |
| 12 | 940 nm IR LED | 1 | ≥ 100 mA continuous, ≥ 1 A pulse (TSAL6200-class) | IR |
| 13 | NPN transistor | 1 | ≥ 500 mA collector, V<sub>CE(sat)</sub> ≤ 0.3 V at 100 mA (BC337 / 2N2222-class) | IR |
| 14 | Resistors 33 Ω (≥ 0.5 W), 1 kΩ, 10 kΩ | 1 each | The driver of §3.1 | IR |
| 15 | 38 kHz IR receiver | 1 | TSOP38238-class, 3.3 V-capable, active-low output; 100 Ω + 4.7 µF supply filter (§3.2) | IR |
| 16 | IR remotes of our own | ≥ 1 per protocol | NEC/NECX, RC5, RC6-0, SIRC-12/15/20; v1-ir check 9 wants ≥ 20 vectors each | IR |

### 1.3 NFC

| # | Part | Qty | Rule | Needed by |
| --- | --- | --- | --- | --- |
| 17 | PN532 breakout | 1 | I²C mode by switch or jumper, address 0x24, 3.3 V; record its own pull-ups | NFC, MOD |
| 18 | Our own tags | 1 each | NTAG213, NTAG215, NTAG216, Ultralight; a MIFARE Classic 1K and an ISO-DEP card for identification only | NFC |

### 1.4 Module bus and ID EEPROMs

| # | Part | Qty | Rule | Needed by |
| --- | --- | --- | --- | --- |
| 19 | 24C02 EEPROM, 3.3 V | 6 | AT24C02-class, DIP-8 or on a carrier; see §3.4 for straps and programming | MOD, RF, LAB |
| 20 | 12-pin module connector pair | 2 | The HARDWARE §6 pinout (2.54 mm header and socket is enough on a breadboard) | MOD |
| 21 | EEPROM programmer | 1 | Writes a 256-byte 24C02 at **3.3 V** (§3.4) | MOD, RF, LAB |

### 1.5 Sub-GHz

| # | Part | Qty | Rule | Needed by |
| --- | --- | --- | --- | --- |
| 22 | CC1101 module, 433 MHz | 2 | 26 MHz crystal, 433 MHz matching; the second one is the "own receiver" option of v1-subghz | RF, LAB |
| 23 | 433 MHz antenna | 2 | Matched to the module's connector (SMA or wire) | RF |
| 24 | Own 433.92 MHz OOK transmitter and receiver | 1 pair | A remote or sensor of ours and the device it controls; datasheet or own capture of its timing | RF |

### 1.6 Lab tools

| # | Part | Qty | Rule | Needed by |
| --- | --- | --- | --- | --- |
| 25 | 330 Ω series resistors | 2 | On `LAB_UART_TX` 38 and `LAB_UART_RX` 39 (HARDWARE §2.4) | LAB |
| 26 | Load resistors 330 Ω, 1 kΩ, 10 kΩ, 100 kΩ, 1 % | 2 each | Drive-strength and pull checks | LAB |
| 27 | 1 nF capacitor | 1 | Edge-rate check | LAB |
| 28 | USB-serial adapter | 1 | **3.3 V TTL** (jumper or datasheet checked, TX level measured before connecting) | LAB |
| 29 | I²C breakouts of our own, 3.3 V | 3 | At known, distinct addresses away from 0x24 and 0x50–0x51; one with a WHO_AM_I register, one with a harmless writable register | LAB |
| 30 | SPI NOR flash breakout | 1 | W25Q-class with a 0x9F JEDEC ID, 3.3 V; wired as a "SPI breakout" module (catalogue entry 34) | LAB |

## 2. Instruments

The procedures were written against these. A row marked *minimum* is what the named checks
cannot do without; a cheaper substitute is acceptable only if it meets the rule.

| # | Instrument | Rule | Needed by |
| --- | --- | --- | --- |
| I1 | Multimeter | DC volts, continuity, 10 MΩ input (LAB check 5 reads pulls through it) | all — *minimum* |
| I2 | Current measurement | A milliammeter or current probe for module and LED currents; an inline USB power meter is enough for whole-board current (V1 exit 6) | core, IR, NFC, MOD, RF, LAB |
| I3 | Logic analyser | ≥ 8 channels at ≥ 100 MS/s (LAB: 40 kHz PWM, a 1.09 µs bit at 921 600 Bd). The common 24 MS/s 8-channel clones meet IR, NFC, MOD and RF (≥ 10 MS/s) but not LAB | IR, NFC, MOD, RF, LAB — *minimum* |
| I4 | Oscilloscope | ≥ 100 MHz, 10× probes: I²C rise times, edge rates, contention | MOD, RF, LAB |
| I5 | Signal generator | 3.3 V logic output (0 V / 3.3 V, **not** its 5 V default; verified on I4 before the first connection), 1 Hz–100 kHz, adjustable duty, frequency reference better than 0.01 % | LAB |
| I6 | Spectrum analyser or calibrated SDR | Covers 433 MHz; with a 20–30 dB attenuator and a 50 Ω load for conducted power at `PATABLE` 0x60 | RF — *minimum* for RF checks 18 and 20–23 |
| I7 | Phone | Camera that shows IR (most front cameras); an NFC tag reader app | IR, NFC |
| I8 | Stopwatch-grade timing | The logic analyser on a GPIO is enough for `boot: ui_ready` ≤ 600 ms and key-to-pixel ≤ 50 ms (V1 exits 2 and 4) | core |

## 3. Prototype values (ADR-0024)

### 3.1 IR LED driver

`IR_TX` (GPIO 47) → **1 kΩ** → NPN base; **10 kΩ** base to GND, so the LED stays off while the
pin floats during reset. Emitter to GND. Collector → IR LED cathode; LED anode → **33 Ω** →
**5 V** (the DevKit's 5V pin, from USB). Design peak current
(5 V − 1.35 V LED − 0.2 V V<sub>CE</sub>) / 33 Ω ≈ **105 mA**, at the firmware's 33 % carrier
duty about 35 mA average during a mark; base current (3.3 − 0.7) / 1 kΩ ≈ 2.6 mA. This is
the "design value of the driver resistor" v1-ir check 3 measures against. The 5 V node
touches only the LED and the collector.

### 3.2 IR receiver

TSOP38238-class on 3V3 through **100 Ω**, with **4.7 µF** and 100 nF from its supply pin to
GND (the datasheet filter); its output straight to `IR_RX` (GPIO 48), no extra pull-up (the
part has one).

### 3.3 microSD breakout

The card shares SPI2 with the panel (HARDWARE §2.3), so the breakout must **release MISO
when its CS is high**. Use a 3.3 V direct-connect breakout (card pins wired straight to the
header, at most pull-ups and decoupling). Avoid the 5 V "level-shifter" modules with an
on-board regulator and a buffer whose MISO output is always enabled: they hold MISO while
the panel or a module is addressed. V1 exit 8 and MOD check 14 measure the release either
way.

### 3.4 ID EEPROMs

* **Straps.** Slot 0's EEPROM answers at 0x50 (A2 A1 A0 = 000), slot 1's at 0x51
  (A0 = 1); WP to GND; 100 nF on its supply.
* **Contents.** Each chip holds a 32-byte descriptor at offset 0 (docs/module-format.md), the
  rest 0xFF: `scripts/gen_module_vectors.py --emit DIR cc1101_module spi_breakout nfc_carrier
  blank bad_crc` writes those five as 256-byte images for the programmer (STARK-0803). Six chips cover the procedures: the Sub-GHz module (catalogue entry 32, id
  0x0A01, caps 0x38), the SPI breakout (entry 34), the PN532 carrier, one blank (all 0xFF)
  and one with a corrupted CRC for MOD check 3, and one spare.
* **Programming** happens off the device: the firmware cannot write a chip (ADR-0021 §2), and
  the lab tools refuse writes to 0x50/0x51 (ADR-0023 §5). The programmer must drive the chip at
  **3.3 V**: the common CH341A-class programmers output 5 V on their 24-series socket unless
  modified, and are only acceptable with that measured at 3.3 V first.

### 3.5 Shared I²C pull-ups

The firmware enables the ESP32-S3 internal pull-ups (about 45 kΩ, too weak for 400 kHz on
its own). Fit **one pair of 4.7 kΩ** from `I2C_SDA` / `I2C_SCL` to 3V3 on the breadboard
unless a breakout already provides pull-ups; record every pull-up present (PN532, I²C
breakouts, carriers) in MOD check 5 and LAB check 21, and remove extras if the parallel
value falls below 2 kΩ.

## 4. Order of work

Bring-up follows HARDWARE §8: power → flash and boot → display → buttons → SD → radios.
In parts terms: §1.1 and I1–I3 first (core); then §1.4 and §1.3 (MOD, NFC); then §1.2 (IR);
then §1.6 and I4–I5 (LAB); §1.5 and I6 last (RF), since the Sub-GHz checks are the only ones
that transmit.
