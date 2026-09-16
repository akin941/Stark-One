#!/usr/bin/env bash
#
# scripts/test_host.sh — build and run host unit tests
#
# Usage:  scripts/test_host.sh [--coverage]
#   --coverage  Enable gcovr coverage report (requires gcovr installed)
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
HOST_DIR="$PROJECT_ROOT/test/host"
BUILD_DIR="$PROJECT_ROOT/build-host"

ENABLE_COVERAGE=false

for arg in "$@"; do
    case "$arg" in
        --coverage)
            ENABLE_COVERAGE=true
            ;;
        *)
            echo "Usage: scripts/test_host.sh [--coverage]" >&2
            exit 2
            ;;
    esac
done

cd "$PROJECT_ROOT"

CMAKE_ARGS=(
    -S "$HOST_DIR"
    -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE=Debug
    -DCMAKE_C_COMPILER="${CC:-clang}"
)

if [[ "$ENABLE_COVERAGE" == "true" ]]; then
    CMAKE_ARGS+=(-DSTARK_COVERAGE=ON)
fi

echo "Configuring host tests..."
cmake "${CMAKE_ARGS[@]}" > /dev/null

echo "Building host tests..."
cmake --build "$BUILD_DIR" -- -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo "Running host tests..."
ctest --output-on-failure --test-dir "$BUILD_DIR"

# Coverage report (if enabled)
if [[ "$ENABLE_COVERAGE" == "true" ]]; then
    echo "Generating coverage report..."
    if command -v gcovr >/dev/null 2>&1; then
        # We always compile with clang, which emits LLVM-flavored .gcno/.gcda
        # files. The system `gcov` (GCC's) can't read them ("version '408*',
        # prefer version 'B33*'") — gcovr needs llvm-cov's gcov-compatible mode.
        if [[ "$(uname)" == "Darwin" ]]; then
            GCOV_EXECUTABLE="$(xcrun --find llvm-cov) gcov"
        else
            GCOV_EXECUTABLE="llvm-cov gcov"
        fi
        gcovr -r "$PROJECT_ROOT" \
            --filter "$PROJECT_ROOT/components/stark_err" \
            --gcov-executable "$GCOV_EXECUTABLE" \
            --xml --xml-pretty -o "$BUILD_DIR/coverage.xml"
        echo "Coverage report: $BUILD_DIR/coverage.xml"
    else
        echo "WARNING: gcovr not found, skipping coverage report" >&2
    fi
fi

echo "Host tests passed."
