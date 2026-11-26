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

# Gather all C/H sources, excluding build artefacts and the local Python
# virtual environment (`.venv/`, git-ignored; docs/DEVELOPMENT.md §7 creates it
# at the repository root for the host tests' cmake and gcovr, and the headers of
# the Python it bundles are not ours to format).
# A read loop rather than `mapfile -d`, which needs bash 4.4: the maintainer's
# macOS ships bash 3.2 (PROJECT.md §7).
SOURCES=()
while IFS= read -r -d '' src; do
    SOURCES+=("$src")
done < <(find "$PROJECT_ROOT" \
    -name '*.[ch]' \
    -not -path '*/build/*' \
    -not -path '*/build-*/*' \
    -not -path '*/managed_components/*' \
    -not -path '*/.git/*' \
    -not -path '*/.venv/*' \
    -print0 2>/dev/null || true)

if [[ ${#SOURCES[@]} -eq 0 ]]; then
    echo "No C/H source files found to format."
    exit 0
fi

if [[ "$CHECK_ONLY" == "true" ]]; then
    # --Werror turns clang-format's per-line "code should be clang-formatted"
    # warnings into a non-zero exit code, which is the actual pass/fail signal.
    # (There is no "would reformat" string in clang-format's output — checking
    # for one, as a prior version of this script did, always reports success.)
    if ! OUTPUT="$(clang-format --dry-run --Werror "${SOURCES[@]}" 2>&1)"; then
        echo "ERROR: one or more of ${#SOURCES[@]} file(s) need formatting:" >&2
        echo "$OUTPUT" >&2
        echo "Run 'scripts/fmt.sh' to fix." >&2
        exit 1
    fi
    echo "All C/H sources are properly formatted."
else
    clang-format -i "${SOURCES[@]}"
    echo "Formatted ${#SOURCES[@]} file(s)."
fi
