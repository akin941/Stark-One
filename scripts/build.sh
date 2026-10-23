#!/usr/bin/env bash
#
# scripts/build.sh — build the STARK ONE firmware.
#
# Also writes build/merged-binary.bin (idf.py merge-bin): the single flash
# image the esp-emulator tier runs (scripts/test_emu.sh, docs/VALIDATION.md).
#
# Usage:  scripts/build.sh [--docker] [--emu-storage]
#
#   --docker       Build inside the pinned espressif/idf Docker image.
#   --emu-storage  Build the emulator storage test image instead
#                  (test/emu/storage_image, ADR-0018) into build/emu-storage/.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

USE_DOCKER=false
EMU_STORAGE=false

for arg in "$@"; do
    case "$arg" in
        --docker)
            USE_DOCKER=true
            ;;
        --emu-storage)
            EMU_STORAGE=true
            ;;
        *)
            echo "Usage: scripts/build.sh [--docker] [--emu-storage]" >&2
            exit 2
            ;;
    esac
done

cd "$PROJECT_ROOT"

# Read the pinned IDF version.
if [[ ! -f .idf-version ]]; then
    echo "ERROR: .idf-version not found" >&2
    exit 1
fi
IDF_TAG="$(tr -d '[:space:]' < .idf-version)"

# The production image builds from the repository root; the storage test
# image from its own project directory, with its sdkconfig kept in its build
# directory. Absolute paths: the repository is /p inside the container.
ROOT_IN="$PROJECT_ROOT"
[[ "$USE_DOCKER" == "true" ]] && ROOT_IN=/p
IDF_ARGS=()
OUT="build/stark-one.elf, build/merged-binary.bin"
if [[ "$EMU_STORAGE" == "true" ]]; then
    IDF_ARGS=(-C "$ROOT_IN/test/emu/storage_image" -B "$ROOT_IN/build/emu-storage"
              -D "SDKCONFIG=$ROOT_IN/build/emu-storage/sdkconfig")
    OUT="build/emu-storage/stark-one-emu-storage.elf, build/emu-storage/merged-binary.bin"
fi

if [[ "$USE_DOCKER" == "true" ]]; then
    echo "Building with Docker: espressif/idf:${IDF_TAG}"
    if ! command -v docker >/dev/null 2>&1; then
        echo "ERROR: docker not found in PATH" >&2
        exit 1
    fi
    # Mount the project read-write at /p and build inside the container.
    # -e preserves the host user so output files are not owned by root.
    docker run --rm \
        -v "$PROJECT_ROOT:/p" \
        -w /p \
        -e "HOST_UID=$(id -u)" \
        -e "HOST_GID=$(id -g)" \
        "espressif/idf:${IDF_TAG}" \
        idf.py ${IDF_ARGS[@]+"${IDF_ARGS[@]}"} build merge-bin
else
    echo "Building with local ESP-IDF (IDF_TAG=${IDF_TAG})"
    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py not found.  Run scripts/setup.sh first or use --docker." >&2
        exit 1
    fi
    idf.py ${IDF_ARGS[@]+"${IDF_ARGS[@]}"} build merge-bin
fi

echo "Build complete: $OUT"
