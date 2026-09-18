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
    bash -c '! grep -rnE "#if.*(WOKWI|SIM)" main/ components/ apps/ 2>/dev/null'

# --- GPIO centralization (STARK-0006, AC 1) -------------------------------
# Nothing outside components/stark_board may name a GPIO. Grepping for a
# specific set of "GPIO-looking" identifiers is easy to bypass by
# accident; the robust, structural version of this check is: you cannot
# reference a raw pin at all without first including the driver header
# that lets you touch one. If no file outside stark_board includes any of
# ESP-IDF's low-level GPIO/SPI/LEDC/I2C driver headers, no raw pin
# manipulation can exist outside stark_board, full stop.
# shellcheck disable=SC2016 # single-quoted on purpose: $violations is set
# and used inside this bash -c script, not by the outer shell.
check "gpio-centralized" bash -c '
    violations=$(grep -rlE "driver/(gpio|spi_master|ledc|i2c)\.h" main/ components/ apps/ 2>/dev/null \
        | grep -v "^components/stark_board/")
    if [[ -n "$violations" ]]; then
        echo "Raw ESP-IDF pin driver header included outside components/stark_board:" >&2
        echo "$violations" >&2
        exit 1
    fi
    exit 0
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
