#!/usr/bin/env bash
#
# scripts/test_emu.sh — Tier 3: run the esp-emulator scenarios (test/emu/*.toml)
# on the built firmware, locally or in CI, with no account and no quota
# (docs/VALIDATION.md, ADR-0017).
#
# Usage:  scripts/test_emu.sh [--image storage] [scenario.toml ...]
#
# Needs build/merged-binary.bin and build/stark-one.elf:
#   scripts/build.sh --docker   (also writes the merged image)
# or, with --image storage, the emulator storage test image (ADR-0018):
#   scripts/build.sh --docker --emu-storage   (build/emu-storage/;
#   default scenarios test/emu/storage/*.toml)
# Runs scripts/test_emu.py inside the pinned espressif/idf image (it provides
# the Xtensa GDB) with the pinned, SHA-256-verified Linux esp-emu build for
# this machine's architecture (scripts/install_esp_emu.sh). Logs:
# build/emu/<scenario>.log.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

case "$(uname -m)" in
    x86_64 | amd64) TRIPLE=x86_64-unknown-linux-gnu ;;
    arm64 | aarch64) TRIPLE=aarch64-unknown-linux-gnu ;;
    *) echo "ERROR: unsupported architecture $(uname -m)" >&2; exit 2 ;;
esac

IMAGE=production
FIRMWARE=build/merged-binary.bin
ELF=build/stark-one.elf
BUILD_HINT="scripts/build.sh --docker"
if [[ "${1:-}" == "--image" ]]; then
    case "${2:-}" in
        production) ;;
        storage)
            IMAGE=storage
            FIRMWARE=build/emu-storage/merged-binary.bin
            ELF=build/emu-storage/stark-one-emu-storage.elf
            BUILD_HINT="scripts/build.sh --docker --emu-storage"
            ;;
        *) echo "ERROR: --image production|storage" >&2; exit 2 ;;
    esac
    shift 2
fi

for f in "$FIRMWARE" "$ELF"; do
    if [[ ! -f "$f" ]]; then
        echo "ERROR: $f missing — run: $BUILD_HINT" >&2
        exit 2
    fi
done

EMU="$(scripts/install_esp_emu.sh --triple "$TRIPLE")"
EMU_IN_CONTAINER="/p/${EMU#"$PROJECT_ROOT"/}"
IDF_TAG="$(tr -d '[:space:]' < .idf-version)"

args=()
for s in "$@"; do
    args+=("/p/${s#"$PROJECT_ROOT"/}")
done

docker run --rm -v "$PROJECT_ROOT:/p" -w /p "espressif/idf:${IDF_TAG}" \
    python3 scripts/test_emu.py --image "$IMAGE" --emu "$EMU_IN_CONTAINER" \
    --firmware "/p/$FIRMWARE" --elf "/p/$ELF" \
    --log-dir /p/build/emu ${args[@]+"${args[@]}"}
