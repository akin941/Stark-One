#!/usr/bin/env bash
#
# scripts/fmt.sh — format C/H sources with clang-format.
#
# Usage:  scripts/fmt.sh           # apply formatting
#         scripts/fmt.sh --check   # fail (exit 1) if any file is unformatted
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

CHECK_ONLY=false
for arg in "$@"; do
    case "$arg" in
        --check) CHECK_ONLY=true ;;
        *) echo "Usage: scripts/fmt.sh [--check]" >&2; exit 2 ;;
    esac
done

cd "$PROJECT_ROOT"

if ! command -v clang-format >/dev/null 2>&1; then
    echo "ERROR: clang-format not found." >&2
    echo "Install it (macOS: 'brew install clang-format') or use Docker." >&2
    exit 1
fi

# Gather all C/H sources, excluding build artefacts.
mapfile -d '' SOURCES < <(find "$PROJECT_ROOT" \
    -name '*.[ch]' \
    -not -path '*/build/*' \
    -not -path '*/build-*/*' \
    -not -path '*/managed_components/*' \
    -not -path '*/.git/*' \
    -print0 2>/dev/null || true)

if [[ ${#SOURCES[@]} -eq 0 ]]; then
    echo "No C/H source files found to format."
    exit 0
fi

if [[ "$CHECK_ONLY" == "true" ]]; then
    # --dry-run reports "would reformat" for each file that differs.
    OUTPUT="$(clang-format --dry-run "${SOURCES[@]}" 2>&1 || true)"
    UNFORMATTED="$(echo "$OUTPUT" | grep -c 'would reformat' || true)"
    if [[ "$UNFORMATTED" -gt 0 ]]; then
        echo "ERROR: ${#SOURCES[@]} file(s) need formatting:" >&2
        echo "$OUTPUT" >&2
        echo "Run 'scripts/fmt.sh' to fix." >&2
        exit 1
    fi
    echo "All C/H sources are properly formatted."
else
    clang-format -i "${SOURCES[@]}"
    echo "Formatted ${#SOURCES[@]} file(s)."
fi
