#!/usr/bin/env bash
#
# scripts/test_wokwi.sh — run the Wokwi CI scenarios headlessly (STARK-0021).
#
# Usage:  scripts/test_wokwi.sh [scenario.yaml ...]
#         (default: every test/scenarios/*.yaml)
#
# Needs a built firmware (scripts/build.sh), wokwi-cli on PATH (or WOKWI_CLI
# pointing at it) and WOKWI_CLI_TOKEN in the environment. The token is only
# ever passed through to wokwi-cli; this script never prints it.
#
# Scenarios assert presence and ordering of log lines, never wall-clock
# values (ADR-0011). On top of each scenario, this script checks the serial
# log for things a scenario cannot express:
#   - `diag: heap=<n>`, if present, is >= HEAP_MIN (200 kB, ROADMAP V0 exit 6);
#   - no `panic:`, `Guru Meditation` or `ui: render failed` anywhere.
# Serial logs are kept in build/wokwi/<scenario>.log.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

WOKWI_CLI="${WOKWI_CLI:-wokwi-cli}"
HEAP_MIN=200000
TIMEOUT_MS=30000
LOG_DIR="build/wokwi"

if [[ -z "${WOKWI_CLI_TOKEN:-}" ]]; then
    echo "ERROR: WOKWI_CLI_TOKEN is not set" >&2
    exit 2
fi
if ! command -v "$WOKWI_CLI" >/dev/null 2>&1; then
    echo "ERROR: wokwi-cli not found (set WOKWI_CLI or add it to PATH)" >&2
    exit 2
fi
if [[ ! -f build/flasher_args.json || ! -f build/stark-one.elf ]]; then
    echo "ERROR: no firmware in build/ — run scripts/build.sh first" >&2
    exit 2
fi

if [[ $# -gt 0 ]]; then
    SCENARIOS=("$@")
else
    SCENARIOS=(test/scenarios/*.yaml)
fi

mkdir -p "$LOG_DIR"
failed=0
for scenario in "${SCENARIOS[@]}"; do
    name="$(basename "$scenario" .yaml)"
    log="$LOG_DIR/$name.log"
    echo "=== $name"
    if ! "$WOKWI_CLI" . --timeout "$TIMEOUT_MS" --scenario "$scenario" \
            --serial-log-file "$log" --quiet; then
        echo "FAIL: $name — scenario did not complete (serial log: $log)" >&2
        failed=1
        continue
    fi
    if grep -a -q -E 'panic:|Guru Meditation|ui: render failed' "$log"; then
        echo "FAIL: $name — panic or render failure in the serial log:" >&2
        grep -a -E 'panic:|Guru Meditation|ui: render failed' "$log" >&2
        failed=1
        continue
    fi
    heap="$(grep -a -o -E 'diag: heap=[0-9]+' "$log" | head -n 1 | cut -d= -f2 || true)"
    if [[ -n "$heap" ]]; then
        if (( heap < HEAP_MIN )); then
            echo "FAIL: $name — diag: heap=$heap is below $HEAP_MIN" >&2
            failed=1
            continue
        fi
        echo "    diag: heap=$heap (>= $HEAP_MIN)"
    fi
    echo "PASS: $name"
done

exit "$failed"
