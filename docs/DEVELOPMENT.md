# STARK ONE — Development Guide

This document describes how to set up the development environment, build the
firmware, flash it to hardware, and use the Docker-based reproducible build path.

## Pinned Versions (verified 2026-09-15)

| Component          | Version / Tag    | Notes |
|--------------------|------------------|-------|
| ESP-IDF            | **v6.1**         | Latest stable minor release; Docker image `espressif/idf:v6.1` |
| ILI9341 component  | **==2.1.0**      | Pinned in `idf_component.yml` (fallback `==2.0.2`, ADR-0004) |
| Unity (host tests) | **2.5.2**        | Vendored in `test/host/unity/` |
| clang-format       | **17+**          | Matches `.clang-format` style |

ESP-IDF v6.1 was released 2026-08-27 (commit `fff9895`). The `.idf-version` file in the
repository root records the pinned tag. `scripts/setup.sh` enforces this version.

---

## 1. Local ESP-IDF Installation (macOS / Linux)

### 1.1 Prerequisites

- **macOS**: Homebrew (`brew`), Python 3.10+
- **Linux**: `build-essential`, `python3`, `python3-venv`, `git`

### 1.2 Install ESP-IDF v6.1

```bash
# Clone ESP-IDF v6.1 with submodules
git clone -b v6.1 --recursive https://github.com/espressif/esp-idf.git ~/esp/v6.1

# Install tools and dependencies
cd ~/esp/v6.1
./install.sh esp32s3

# Source the environment (add to ~/.zshrc / ~/.bashrc for persistence)
. ./export.sh
```

Verify:
```bash
idf.py --version
# Expected: ESP-IDF v6.1
```

### 1.3 Verify with the project check script

```bash
cd /path/to/stark-one
scripts/setup.sh
scripts/check.sh
```

---

## 2. Docker Build (Reproducible, No Local Install Required)

The Docker path uses the official Espressif image pinned to v6.1. This is the
exact environment used by CI.

```bash
# One-time: pull the image (also done automatically on first build)
docker pull espressif/idf:v6.1

# Build
scripts/build.sh --docker
```

The script mounts the project at `/p` inside the container and runs `idf.py build`.

### 2.1 Interactive Docker Shell

```bash
docker run --rm -it \
    -v "$PWD:/p" \
    -w /p \
    espressif/idf:v6.1 \
    bash
```

Inside the container, `idf.py`, `wokwi-cli`, and all ESP-IDF tools are available.

---

## 3. Build

### 3.1 Local Build

```bash
# First time only: configure target and load defaults
idf.py --target esp32s3 build
# Subsequent builds
idf.py build
```

### 3.2 Docker Build (Recommended for Reproducibility)

```bash
scripts/build.sh --docker
```

### 3.3 Build Output

- ELF: `build/stark-one.elf`
- Binary images: `build/flash_*_40m.bin`, `build/partition_table.bin`
- `flasher_args.json` (used by Wokwi)
- Size report: `idf.py size`

---

## 4. Flash to Hardware

### 4.1 DevKitC-1 (ESP32-S3-DevKitC-1-N16R8)

Connect the board via USB-C. It enumerates as a USB-Serial-JTAG device (CDC/ACM).

```bash
# Find the port (macOS: /dev/cu.usbserial-*, Linux: /dev/ttyUSB* or /dev/ttyACM*)
ls /dev/tty.*

# Flash all partitions
idf.py -p /dev/tty.usbserial-XXXX flash
# Or with Docker:
docker run --rm -v "$PWD:/p" -w /p --device /dev/tty.usbserial-XXXX \
    espressif/idf:v6.1 idf.py -p /dev/tty.usbserial-XXXX flash
```

The bootloader, partition table, and application are flashed in one step.

---

## 5. Serial Monitor

### 5.1 Local

```bash
idf.py -p /dev/tty.usbserial-XXXX monitor
# Exit: Ctrl+]
```

### 5.2 Docker

```bash
docker run --rm -it \
    -v "$PWD:/p" \
    -w /p \
    --device /dev/tty.usbserial-XXXX \
    espressif/idf:v6.1 \
    idf.py -p /dev/tty.usbserial-XXXX monitor
```

Baud rate is 115200 (UART0, GPIO 43/44) as per `CONFIG_ESP_CONSOLE_UART_DEFAULT=y`.

---

## 6. Wokwi Simulation

The simulator runs the **same build artifact** as real hardware (ADR-0010).

### 6.1 VS Code Extension (Interactive)

1. Install the "Wokwi for VS Code" extension.
2. Open the repository root.
3. Press `F1` → **Wokwi: Start Simulator**.

### 6.2 Headless CLI (CI / Automation)

```bash
# One-time
npm install -g wokwi-cli

# Build first, then run
scripts/build.sh --docker
wokwi-cli . --timeout 20000 --scenario test/scenarios/v0-boot-and-menu.yaml
```

The CI workflow (`.github/workflows/ci.yml`) runs scenarios via the GitHub Action
`wokwi/wokwi-ci-action`.

---

## 7. Code Quality Gates

All scripts use `set -euo pipefail` and return non-zero on errors.

| Script            | Purpose |
|-------------------|---------|
| `scripts/setup.sh`   | Verify ESP-IDF version matches `.idf-version` |
| `scripts/build.sh`   | Build locally or via `--docker` |
| `scripts/fmt.sh`     | Format sources with clang-format (`--check` for CI) |
| `scripts/check.sh`   | Repository structure, shell syntax, ADR-0010 compliance |
| `scripts/test_host.sh` | Host unit tests (added by STARK-0005) |
| `scripts/test_wokwi.sh` | Headless Wokwi scenarios (added by STARK-0021) |

Run locally before pushing:
```bash
scripts/check.sh
scripts/fmt.sh --check
scripts/build.sh          # or scripts/build.sh --docker
```

---

## 8. Project Structure Overview

```
stark-one/
├── .idf-version              # Pinned ESP-IDF tag (v6.1)
├── CMakeLists.txt            # ESP-IDF project root
├── sdkconfig.defaults        # Shared Kconfig defaults
├── sdkconfig.defaults.esp32s3 # ESP32-S3 target-specific defaults
├── partitions.csv            # V0: nvs 24K, phy_init 4K, factory 4M
├── main/
│   ├── CMakeLists.txt        # Main component registration
│   └── stark_main.c          # Minimal app_main (STARK-0001)
├── components/               # Firmware components (STARK-0002+)
├── apps/                     # Application modules (STARK-0019+)
├── test/
│   ├── host/                 # Host unit tests (STARK-0005+)
│   └── scenarios/            # Wokwi CI scenarios (STARK-0021+)
├── scripts/                  # Build & check automation
├── docs/                     # Documentation
└── wokwi.toml, diagram.json  # Wokwi config (STARK-0003+)
```

---

## 9. Troubleshooting

| Symptom | Resolution |
|---------|------------|
| `scripts/setup.sh` fails with version mismatch | Install ESP-IDF v6.1 or use `--docker` |
| `idf.py build` fails with "No such file" | Run `idf.py --target esp32s3 set-target` first |
| Docker build: "permission denied" on output files | Script uses `-e HOST_UID=$(id -u) -e HOST_GID=$(id -g)` |
| `clang-format` not found | `brew install clang-format` (macOS) or `apt install clang-format-17` (Linux) |
| Wokwi: "flash size mismatch" | Verify `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y` in `sdkconfig.defaults` |
| Serial monitor shows garbled text | Ensure 115200 baud, 8N1, no flow control |

---

## 10. Useful Commands Cheat Sheet

```bash
# Clean build
idf.py fullclean

# Size report
idf.py size

# Menuconfig (interactive Kconfig)
idf.py menuconfig

# Generate compile_commands.json for clangd
idf.py reconfigure

# Run only host tests (when available)
scripts/test_host.sh

# Run Wokwi smoke test (when available)
scripts/test_wokwi.sh
```

---

## 11. Updating the Pinned ESP-IDF Version

Version bumps are a deliberate task (ADR-0003 revisit trigger). To update:

1. Verify the new tag on [espressif/esp-idf/releases](https://github.com/espressif/esp-idf/releases).
2. Update `.idf-version` with the new tag (e.g. `v6.2`).
3. Update `scripts/build.sh` and CI workflow Docker image tag.
4. Run `scripts/build.sh --docker` to verify the build passes.
5. If component dependencies exist, run `idf.py add-dependency` and commit the updated `dependencies.lock`.
6. Record the change and date in this document (`Pinned Versions` table).

---

*Last verified: 2026-09-15. Pinned versions updated with each version bump task.*