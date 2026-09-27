#!/usr/bin/env bash
#
# scripts/check.sh — repository-level static checks for STARK ONE.
#
# Runs every check that can succeed at the current milestone.
# Later tasks register additional checks (check_layers.py, check_pins.py,
# gitleaks, …) — see AGENTS.md and TESTING.md.
#
# Usage:  scripts/check.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

PASS=0
FAIL=0

check() {
    local name="$1"; shift
    if "$@"; then
        echo "  PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $name"
        FAIL=$((FAIL + 1))
    fi
}

echo "Running repository checks..."

# --- Shell syntax --------------------------------------------------------
check "shell-syntax-setup"  bash -n scripts/setup.sh
check "shell-syntax-build"  bash -n scripts/build.sh
check "shell-syntax-fmt"    bash -n scripts/fmt.sh
check "shell-syntax-check"  bash -n scripts/check.sh
check "shell-syntax-test-host"  bash -n scripts/test_host.sh
check "shell-syntax-test-wokwi" bash -n scripts/test_wokwi.sh
check "shell-syntax-test-emu"   bash -n scripts/test_emu.sh
check "shell-syntax-install-esp-emu" bash -n scripts/install_esp_emu.sh

# --- Layer rules (ARCHITECTURE.md §2, §10; STARK-0100) --------------------
check "layers" python3 scripts/check_layers.py

# --- Script unit tests: emulator harness, Wokwi gate ----------------------
check "script-unit-tests" python3 -m unittest discover -s scripts -p 'test_*.py' -q

# --- Required files ------------------------------------------------------
for f in CMakeLists.txt sdkconfig.defaults partitions.csv .idf-version \
         main/CMakeLists.txt main/stark_main.c; do
    check "exists-$f" test -f "$f"
done

# --- .idf-version non-empty ---------------------------------------------
check "idf-version-set" bash -c '[[ -s .idf-version ]]'

# --- No simulator conditionals (ADR-0010) --------------------------------
# grep for #ifdef / #ifdef WOKWI / #ifdef SIM in source files only.
check "no-wokwi-ifdef" \
    bash -c '! grep -rnE "#if.*(WOKWI|SIM|QEMU|ESP_EMU)" main/ components/ apps/ 2>/dev/null'

# --- No simulator references in firmware code (AGENTS.md, STARK-0021 AC 3) --
check "no-wokwi-mention" \
    bash -c '! grep -rqi wokwi main/ components/ apps/ 2>/dev/null'

# --- GPIO centralization (STARK-0006 AC 1, STARK-0007) --------------------
# Nothing outside components/stark_board or components/stark_hal may name a
# GPIO or touch ESP-IDF's driver layer. Grepping for a specific set of
# "GPIO-looking" identifiers is easy to bypass by accident; the robust,
# structural version of this check is: you cannot reference a raw pin at
# all without first including the driver header that lets you touch one.
# stark_board owns the pin *numbers* (gpio.h, spi_master.h — it owns SPI2
# init); stark_hal owns the pin *mechanism* (gpio.h, ledc.h — TASKS.md
# STARK-0007: "No SPI wrapper", so spi_master.h/i2c.h stay board-only too).
# shellcheck disable=SC2016 # single-quoted on purpose: variables are set
# and used inside this bash -c script, not by the outer shell.
check "gpio-centralized" bash -c '
    violations=$(grep -rlE "driver/(gpio|spi_master|ledc|i2c)\.h" main/ components/ apps/ 2>/dev/null \
        | grep -v "^components/stark_board/" | grep -v "^components/stark_hal/")
    hal_violations=$(grep -rlE "driver/(spi_master|i2c)\.h" components/stark_hal/ 2>/dev/null || true)
    if [[ -n "$violations" || -n "$hal_violations" ]]; then
        if [[ -n "$violations" ]]; then
            echo "Raw ESP-IDF pin driver header included outside stark_board/stark_hal:" >&2
            echo "$violations" >&2
        fi
        if [[ -n "$hal_violations" ]]; then
            echo "stark_hal must not wrap SPI/I2C (TASKS.md STARK-0007: \"No SPI wrapper\"):" >&2
            echo "$hal_violations" >&2
        fi
        exit 1
    fi
    exit 0
'

# --- Font table reproducible (STARK-0014 AC 4) ---------------------------
# tools/fontconv.py must regenerate the committed subset BDF and C table
# byte-for-byte (commands: components/stark_gfx/fonts/README.md).
# shellcheck disable=SC2016 # single-quoted on purpose (see gpio-centralized)
check "font-reproducible" bash -c '
    set -e
    tmp=$(mktemp -d); trap "rm -rf \"\$tmp\"" EXIT
    fonts=components/stark_gfx/fonts
    python3 tools/fontconv.py subset "$fonts/misc-fixed-8x13-ascii.bdf" "$tmp/subset.bdf" \
        --first 0x20 --last 0x7E
    cmp -s "$fonts/misc-fixed-8x13-ascii.bdf" "$tmp/subset.bdf"
    python3 tools/fontconv.py generate "$fonts/misc-fixed-8x13-ascii.bdf" "$tmp/font_mono16.c" \
        --name gfx_font_mono16 --cell 8x16 --baseline 13 --first 0x20 --last 0x7E --fallback 0x3F
    sed "s|$tmp/font_mono16.c|font_mono16.c|" "$tmp/font_mono16.c" | cmp -s - "$fonts/font_mono16.c"
'

# --- Git status after build (no tracked generated files) -----------------
# This check is also run manually; here we verify .gitignore covers the
# standard ESP-IDF build outputs.
check "gitignore-has-build"   grep -q '^build/'        .gitignore
check "gitignore-has-sdkcfg"  grep -q '^sdkconfig$'    .gitignore
check "gitignore-has-mgd"     grep -q '^managed_components/' .gitignore

echo ""
echo "Checks: $PASS passed, $FAIL failed."
if [[ "$FAIL" -gt 0 ]]; then
    exit 1
fi
echo "All checks passed."
