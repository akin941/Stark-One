#!/usr/bin/env bash
#
# scripts/install_esp_emu.sh — fetch the pinned Espressif esp-emulator and
# verify its SHA-256 (scripts/esp_emu.lock). Prints the binary's path.
#
# Usage:  scripts/install_esp_emu.sh [--triple TRIPLE] [--dir DIR]
#
#   --triple  x86_64-unknown-linux-gnu | aarch64-unknown-linux-gnu |
#             aarch64-apple-darwin (default: this machine)
#   --dir     cache directory (default: .cache/esp-emu in the repository)
#
# The tarball is kept in the cache and re-verified on every call; a checksum
# mismatch deletes it and fails. Nothing unverified is ever extracted or run.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOCK="$SCRIPT_DIR/esp_emu.lock"
CACHE="$PROJECT_ROOT/.cache/esp-emu"
TRIPLE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --triple) TRIPLE="$2"; shift 2 ;;
        --dir) CACHE="$2"; shift 2 ;;
        *) echo "Usage: scripts/install_esp_emu.sh [--triple TRIPLE] [--dir DIR]" >&2; exit 2 ;;
    esac
done

if [[ -z "$TRIPLE" ]]; then
    case "$(uname -s)-$(uname -m)" in
        Linux-x86_64) TRIPLE=x86_64-unknown-linux-gnu ;;
        Linux-aarch64 | Linux-arm64) TRIPLE=aarch64-unknown-linux-gnu ;;
        Darwin-arm64) TRIPLE=aarch64-apple-darwin ;;
        *) echo "ERROR: no pinned esp-emu build for $(uname -s)-$(uname -m)" >&2; exit 1 ;;
    esac
fi

lock_value() {
    local v
    v="$(grep -E "^$1=" "$LOCK" | head -n 1 | cut -d= -f2-)"
    if [[ -z "$v" ]]; then
        echo "ERROR: '$1' missing from $LOCK" >&2
        exit 1
    fi
    printf '%s' "$v"
}

VERSION="$(lock_value version)"
SHA256="$(lock_value "sha256.$TRIPLE")"
URL="$(lock_value url)"
URL="${URL//\{version\}/$VERSION}"
URL="${URL//\{triple\}/$TRIPLE}"

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

DIR="$CACHE/$VERSION"
TARBALL="$DIR/esp-emu-$VERSION-$TRIPLE.tar.gz"
BIN="$DIR/esp-emu-$VERSION-$TRIPLE/esp-emu"
mkdir -p "$DIR"

if [[ ! -f "$TARBALL" ]]; then
    echo "Downloading esp-emu $VERSION ($TRIPLE)..." >&2
    curl -fsSL --retry 3 -o "$TARBALL.part" "$URL"
    mv "$TARBALL.part" "$TARBALL"
fi

actual="$(sha256_of "$TARBALL")"
if [[ "$actual" != "$SHA256" ]]; then
    echo "ERROR: SHA-256 mismatch for $TARBALL" >&2
    echo "  expected $SHA256" >&2
    echo "  actual   $actual" >&2
    rm -f "$TARBALL"
    exit 1
fi

rm -rf "$DIR/esp-emu-$VERSION-$TRIPLE"
tar -xzf "$TARBALL" -C "$DIR"
if [[ ! -x "$BIN" ]]; then
    echo "ERROR: $BIN not found after extraction" >&2
    exit 1
fi
echo "esp-emu $VERSION ($TRIPLE) verified: sha256 $SHA256" >&2
echo "$BIN"
