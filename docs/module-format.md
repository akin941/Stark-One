# Module ID EEPROM format

Normative for STARK-0501 (ADR-0021). A module's 24C02-class ID EEPROM holds one
descriptor at offset 0. Multi-byte fields are little-endian. The firmware only reads
these 32 bytes and has no way to write a chip. Programming happens off the device.

| Offset | Size | Field | Rule |
| --- | --- | --- | --- |
| 0 | 2 | Magic | `53 4D` ("SM") |
| 2 | 1 | Format version | 1 |
| 3 | 1 | Reserved | 0 |
| 4 | 2 | Module ID | Not 0x0000 or 0xFFFF |
| 6 | 1 | Hardware revision | Any |
| 7 | 1 | Reserved | 0 |
| 8 | 4 | Capability bits | `stark_caps.h`, TASKS.md V0.5 |
| 12 | 18 | Name | 1–18 printable ASCII bytes, no leading space, NUL-padded, nothing after the first NUL |
| 30 | 2 | CRC-16/CCITT-FALSE | Over bytes 0–29 (poly 0x1021, init 0xFFFF; check value of "123456789" is 0x29B1) |

## Results

| Result | When | Effect |
| --- | --- | --- |
| `ok` | All rules hold | A known entry, if its capability combination is supported |
| `blank` | All 32 bytes are 0xFF | Generic entry, no capabilities |
| `bad magic` | Bytes 0–1 differ | Generic entry, no capabilities |
| `bad version` | Byte 2 is not 1 | Generic entry, no capabilities |
| `bad crc` | The CRC does not match | Generic entry, no capabilities |
| `bad field` | A reserved byte, the ID or the name breaks its rule | Generic entry, no capabilities |
| `truncated` | Fewer than 32 bytes were read | Treated as a read error |

## What a descriptor is not

A descriptor is device metadata. It declares what a module says it is. It is not proof of
identity and grants nothing: a capability counts only once a driver in this firmware
confirms it (ADR-0021 §2). Nothing is loaded or executed from an EEPROM.

Unknown capability bits are shown in the inspector and otherwise ignored, so a future
module is listed rather than refused.

The vectors in `test/fake/fake_module_catalog.h` come from an independent encoder,
`scripts/gen_module_vectors.py`, and were never read from a real module. A real 24C02 is
checked at V1 (`docs/bringup/v1-modules.md`).
