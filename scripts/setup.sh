#!/usr/bin/env bash
#
# scripts/setup.sh — verify the development environment for STARK ONE.
#
# Asserts that the ESP-IDF version matches .idf-version.  Does NOT install
# packages, does NOT modify the system, and does NOT use sudo.
#
# Usage:  scripts/setup.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
err()  { echo "ERROR: $*" >&2; exit 1; }
warn() { echo "WARNING: $*" >&2; }
info() { echo "$*"; }

# ---------------------------------------------------------------------------
# 1. Read expected IDF version from .idf-version
# ---------------------------------------------------------------------------
IDF_VERSION_FILE="$PROJECT_ROOT/.idf-version"
if [[ ! -f "$IDF_VERSION_FILE" ]]; then
    err ".idf-version not found at $IDF_VERSION_FILE"
fi
EXPECTED="$(tr -d '[:space:]' < "$IDF_VERSION_FILE")"
if [[ -z "$EXPECTED" ]]; then
    err ".idf-version is empty"
fi
info "Expected ESP-IDF version: $EXPECTED"

# ---------------------------------------------------------------------------
# 2. Locate the ESP-IDF installation
# ---------------------------------------------------------------------------
# Prefer the IDF_PATH environment variable (set by export.sh / export.zsh).
IDF_BIN_FOUND=false

if [[ -n "${IDF_PATH:-}" ]] && [[ -x "${IDF_PATH}/tools/idf_version.py" ]]; then
    ACTUAL="$("${IDF_PATH}/tools/idf_version.py" 2>/dev/null || true)"
    if [[ -n "$ACTUAL" ]]; then
        IDF_BIN_FOUND=true
        info "Detected ESP-IDF at: $IDF_PATH"
        info "Detected ESP-IDF version: $ACTUAL"
    fi
fi

# Fall back: try idf.py from PATH.
if [[ "$IDF_BIN_FOUND" == "false" ]]; then
    if command -v idf.py >/dev/null 2>&1; then
        ACTUAL="$(idf.py --version 2>/dev/null || true)"
        if [[ -n "$ACTUAL" ]]; then
            IDF_BIN_FOUND=true
            info "Detected ESP-IDF via idf.py: $ACTUAL"
        fi
    fi
fi

if [[ "$IDF_BIN_FOUND" == "false" ]]; then
    warn "ESP-IDF $EXPECTED not found in this environment."
    warn "Either source your ESP-IDF export script (e.g. . \$HOME/esp/$EXPECTED/export.sh)"
    warn "or build with Docker:  scripts/build.sh --docker"
    exit 1
fi

# ---------------------------------------------------------------------------
# 3. Compare versions
# ---------------------------------------------------------------------------
# Extract the numeric version tag (e.g. "v6.1.2") from the raw banner.
# The IDF_VERSION env var (set by export.sh) is preferred; otherwise parse
# idf.py --version output.
ACTUAL_TAG="${IDF_VERSION:-}"
if [[ -z "$ACTUAL_TAG" ]]; then
    # Strip non-version text; keep a v-prefixed tag like v6.1 or v6.1.2
    ACTUAL_TAG="$(echo "$ACTUAL" | grep -oE 'v[0-9]+\.[0-9]+(\.[0-9]+)?' | head -1)"
fi

if [[ -z "$ACTUAL_TAG" ]]; then
    err "Could not determine the running ESP-IDF version.  Aborting."
fi

info "Running ESP-IDF:   $ACTUAL_TAG"
info "Required ESP-IDF:  $EXPECTED"

# Compare major.minor (ignore patch-level differences for practicality,
# but still warn loudly).
expected_base="$(echo "$EXPECTED" | grep -oE '[0-9]+\.[0-9]+')"
actual_base="$(echo "$ACTUAL_TAG"  | grep -oE '[0-9]+\.[0-9]+')"

if [[ "$expected_base" != "$actual_base" ]]; then
    err "ESP-IDF version mismatch: you have $ACTUAL_TAG but this project requires $EXPECTED."
    warn "Install ESP-IDF $EXPECTED and re-run, or use:  scripts/build.sh --docker"
    exit 1
fi

# Warn (not error) on patch-level differences within the same minor.
if [[ "$EXPECTED" != "$ACTUAL_TAG" ]]; then
    warn "Patch-level difference: pinned $EXPECTED, running $ACTUAL_TAG."
    warn "Using the same minor series is acceptable; for exact reproducibility use Docker."
fi

info "ESP-IDF environment OK."
