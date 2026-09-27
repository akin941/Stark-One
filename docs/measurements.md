# Measurements — informational only

Timing figures from the simulator are **trend data, never a gate** (ADR-0011,
TESTING.md §1.1). Hardware gates are measured on the physical prototype (V1).

| Date | Task | Environment | Metric | Value | Target (hardware) |
| --- | --- | --- | --- | --- | --- |
| 2026-09-27 | STARK-0016 | Wokwi, ESP32-S3 DevKitC-1, SPI 40 MHz, band 40 | `display: full refresh <N> ms` | 132 ms | ≤ 60 ms at V1 (ADR-0004: ~25 ms SPI time at 40 MHz) |
| 2026-09-27 | STARK-0020 | Wokwi, same; Display Test redrawing its 320×224 area every frame | `displaytest: fps=<N>` | 5.5 fps | FPS gate is a hardware criterion (ADR-0011) |

Wokwi's SPI timing is not the panel's, so the simulated full-refresh figure says nothing
about the hardware target; it is recorded to spot regressions in the firmware's own
per-band work (rendering + byte swap) between runs.
