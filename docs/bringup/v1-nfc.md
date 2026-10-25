# V1 bring-up — NFC (deferred V0.4 gates)

These are the physical half of ROADMAP V0.4's exit criteria, carried to the V1 prototype
by ADR-0020 and listed as ROADMAP V1 exit 10. **Nothing here has been done.**

V0.4 is complete as *NFC software / protocol verified*. The evidence for that is host
vectors and fault injection, the production PN532 driver run against a test-only fake
PN532 at its I²C transport seam, the ESP-IDF build, and the firmware in esp-emu (an
empty I²C bus on the production image; the fake transport on the test image;
docs/VALIDATION.md §3.7). Neither the fake nor the emulator reads a real tag, drives
an antenna or makes an RF field, so none of the checks below is covered by them. A row
is PASS only with the measurement or observation written in it.

**Scope (SECURITY_SCOPE §2, §3).** Every tag used below is our own. MIFARE Classic and
ISO-DEP tags are identified only: no sector authentication, no keys.

## Setup

| Item | Value | Source |
| --- | --- | --- |
| Board | ESP32-S3-DevKitC-1 | HARDWARE.md |
| `I2C_SDA` / `I2C_SCL` | GPIO 1 / GPIO 2, `I2C_NUM_0`, internal pull-ups on (record the module's own pull-ups) | `stark_board`, HARDWARE.md §2.2 |
| Module | PN532 breakout in I²C mode (record the switch / jumper setting), address 0x24 | ADR-0020 |
| Clock | 100 kHz (`STARK_NFC_I2C_HZ`) | `components/stark_nfc/Kconfig.stark` |
| Firmware | production image, the main SHA under test: `________` | — |
| Tags | our own NTAG213, NTAG215, NTAG216, Ultralight; a MIFARE Classic 1K and an ISO-DEP card for identification only | — |
| Instruments | logic analyser, multimeter or current probe, a phone with an NFC tag reader app | — |

## Checklist

| # | Check | How | Expected | Result | Date / who |
| --- | --- | --- | --- | --- | --- |
| 1 | I²C bus on silicon | Logic analyser on GPIO 1/2 at boot | Clean edges at 100 kHz; the module ACKs 0x24; `nfc: pn532 fw 1.6` (or the real revision) | | |
| 2 | Detection and initialisation time | Boot log timestamps | `nfc:` line after `ir: init`; record the delay | | |
| 3 | Module absent at boot | Boot without the module | `nfc: no module (Not found)`; NFC app shows "No NFC module" and the field never switches on | | |
| 4 | Module unplugged while running | Pull the module with the NFC app open | `nfc: module lost`, the app shows it, no panic; plugged back: `nfc: module back`, polling resumes | | |
| 5 | Field only while the app is open | Current probe, or a phone's NFC detector, with the app closed and open | Field on only between `nfc: field on` / `off` | | |
| 6 | Current with the field on and off | Multimeter on the module supply | Record both | | |
| 7 | ≥ 20/20 reads per tag type | Present each own tag 20 times | 20/20 `nfc: tag <tech> uid=…` with the same UID and technology each time | | |
| 8 | Identification of Classic and ISO-DEP | Own cards | `CLASSIC_1K` / `ISO_DEP` with ATQA/SAK; no NDEF read attempted | | |
| 9 | NDEF written by a phone, read by the device | Phone writes a URI and a Text record to our own NTAG | The device shows the same records (`nfcapp: record …`) | | |
| 10 | NDEF written by the device, read by a phone | NFC app → Write URI → confirm | `nfc: write ok`; the phone opens the same URI | | |
| 11 | Write interrupted | Pull the tag away during a write | `nfc: write failed` with a partial-write note, never "wrote"; the tag reads back as it is | | |
| 12 | Read-only and too-small tags | Our own read-only NTAG; a message larger than the capacity | Write refused; nothing written | | |
| 13 | Detection range and orientation | Each tag at 0, 1, 2, 4 cm; flat and at 45° / 90° | Record the furthest reliable read per tag | | |
| 14 | Removal latency | Pull a tag | `nfc: tag left` within ~0.5 s (two missed polls at 250 ms); record it | | |
| 15 | Weak, noisy and two nearby tags | A tag at the edge of range; two tags stacked | Record: one tag read at a time (the first target), no stale data | | |
| 16 | Dump of an own tag | NFC app → Save dump | `nfcapp: saved nfc_NNNN.nfc`; on a PC the file parses and matches the tag's pages | | |
| 17 | I²C bus shared with the rest of the device | NFC polling while the display renders and the SD card writes | No I²C errors; FPS and storage unaffected (record) | | |

Results also go into `docs/measurements.md` → "Hardware (gated, V1 prototype)", NFC rows.
