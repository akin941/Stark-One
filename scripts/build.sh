#!/usr/bin/env bash
#
# scripts/build.sh — build the STARK ONE firmware.
#
# Usage:  scripts/build.sh [--docker]
#
#   --docker   Build inside the pinned espressif/idf Docker image.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

USE_DOCKER=false

for arg in "$@"; do
    case "$arg" in
        --docker)
            USE_DOCKER=true
            ;;
        *)
            echo "Usage: scripts/build.sh [--docker]" >&2
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
        idf.py build
else
    echo "Building with local ESP-IDF (IDF_TAG=${IDF_TAG})"
    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py not found.  Run scripts/setup.sh first or use --docker." >&2
        exit 1
    fi
    idf.py build
fi

echo "Build complete: build/stark-one.elf"
