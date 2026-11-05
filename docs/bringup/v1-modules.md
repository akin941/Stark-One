# V1 bring-up — module bus (deferred V0.5 gates)

These are the physical half of ROADMAP V0.5's exit criteria, carried to the V1 prototype
by ADR-0021 and listed as ROADMAP V1 exit 11. **Nothing here has been done.**

V0.5 is complete as *software / module-protocol verified*. The evidence for that is host
vectors and fault injection, production discovery run against a test-only fake 24C02 at
its transport seam, the ESP-IDF build, and the firmware in esp-emu (docs/VALIDATION.md
§3.8). Neither the fake nor the emulator has a connector, a pull-up, a supply or an SPI
signal, so none of the checks below is covered by them. A row is PASS only with the
measurement or observation written in it.

**Scope (SECURITY_SCOPE).** A descriptor is metadata, not a credential. Program test
EEPROMs off the device: the firmware cannot write one.

## Setup

| Item | Value | Source |
| --- | --- | --- |
| Board | ESP32-S3-DevKitC-1 | HARDWARE.md |
| `I2C_SDA` / `I2C_SCL` | GPIO 1 / GPIO 2 (record pull-up values) | HARDWARE.md §2.2 |
| Slot 0 | EEPROM 0x50, `MOD_CS` 40, `MOD_IRQ0` 41, `MOD_IRQ1` 42 | HARDWARE.md §6 |
| Slot 1 | EEPROM 0x51; lines not allocated yet | ADR-0021 §5 |
| Test EEPROMs | 24C02 programmed from `test/fake/fake_module_catalog.h` images | docs/module-format.md |
| Firmware | production image, the main SHA under test: `________` | — |

## Decisions the board must make first

| # | Decision | Options noted so far | Decided |
| --- | --- | --- | --- |
| D1 | GPIO for `MOD_EN` | All 25 usable GPIOs are allocated (HARDWARE.md §1, §2). An I/O expander, or a V0.7 lab UART pin | |
| D2 | Chip select and IRQ lines for a second wired slot | As D1 | |
| D3 | A module presence pin | None on the 12-pin connector today, so a module without an EEPROM cannot be told from an empty slot | |

## Checks

| # | Check | Pass criterion | Result | Evidence |
| --- | --- | --- | --- | --- |
| 1 | Real 24C02 read | A programmed chip in slot 0 logs its name, ID, revision and caps | | |
| 2 | Address straps | Slots 0 and 1 answer only at 0x50 and 0x51 | | |
| 3 | Blank and corrupt chips | Generic entry with the right reason; boot unaffected | | |
| 4 | No EEPROM | "No ID"; boot unaffected | | |
| 5 | Pull-ups and signal integrity | Rise time and levels recorded on a scope at 100 kHz, with the PN532 and two EEPROMs on the bus | | |
| 6 | Connector pinout | Continuity of all 12 pins against HARDWARE.md §6 | | |
| 7 | Supply limits | 3V3 per module ≤ 300 mA budget; VBAT behaviour recorded | | |
| 8 | Physical insertion | Inserted into a running device: appears within 3 s, no reset, no display glitch | | |
| 9 | Physical removal | Removed from a running device: gone within 3 s, no reset, UI responsive | | |
| 10 | Hotplug electrical safety | No brown-out on the rail during insertion; inrush recorded; no back-powering | | |
| 11 | Bus recovery | A module leaving mid-transaction, or holding SDA low: the bus recovers without a reset. Record whether a recovery routine is needed | | |
| 12 | Two modules share SPI (ROADMAP V0.5 exit 3) | Two modules on SPI2 with the panel rendering and the SD card busy: no display corruption, logic analyser capture attached. Needs D2 | | |
| 13 | Chip-select isolation | Module CS high during panel and SD traffic; a module ignores them; pull-up holds before the driver claims it | | |
| 14 | MISO release | Each module releases MISO when deselected | | |
| 15 | Bus contention and timing | Worst band-flush stall with module traffic; per-device clocks correct | | |
| 16 | Module power-up | Time from insertion until the EEPROM and a PN532 answer. Confirm the 2 s and 1 s polls see it | | |
| 17 | `MOD_EN` | Per D1: power-cycles a module; default state safe at boot | | |
| 18 | IRQ lines | `MOD_IRQ0/1` reach a claimed driver; floating when no module is fitted is safe | | |
| 19 | NFC on the final topology | Reader removed and restored: NFC row dims and returns. Bare breakout and EEPROM carrier both (docs/bringup/v1-nfc.md) | | |
| 20 | IR on the final topology | IR row enabled with the onboard front-end (docs/bringup/v1-ir.md) | | |
| 21 | App list without a firmware change (ROADMAP V0.5 exit 1) | Plug a real module: the app list changes, no reflash | | |
