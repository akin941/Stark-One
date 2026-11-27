#!/usr/bin/env bash
#
# scripts/tidy.sh — clang-tidy over the pure sources (STARK-0808, TESTING.md §3, ADR-0026)
#
# Usage:  scripts/tidy.sh [--list]
#   --list  print the sources that are checked and the ones that are not, then exit
#
# The checked set is every file under components/, apps/ and main/ that the host test
# build compiles: its compile_commands.json gives clang the exact flags. The files the
# host build cannot compile (the ESP-IDF adapters) are not checked; --list names them.
# The checks are in .clang-tidy. Any finding fails (exit 1).
#
# CLANG_TIDY selects the binary (default: clang-tidy). JOBS sets the parallelism.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-tidy"
TIDY="${CLANG_TIDY:-clang-tidy}"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

LIST=false
for arg in "$@"; do
    case "$arg" in
        --list) LIST=true ;;
        *)
            echo "Usage: scripts/tidy.sh [--list]" >&2
            exit 2
            ;;
    esac
done

cd "$PROJECT_ROOT"

if ! command -v "$TIDY" >/dev/null 2>&1; then
    echo "ERROR: $TIDY not found (CI: apt-get install clang-tidy; set CLANG_TIDY to another binary)" >&2
    exit 2
fi

# Configuring is enough: clang-tidy needs the compile commands, not the objects.
cmake -S test/host -B "$BUILD_DIR" -DCMAKE_C_COMPILER="${CC:-clang}" > /dev/null

FILES="$BUILD_DIR/tidy-files.txt"
mkdir -p "$BUILD_DIR/db"
python3 - "$BUILD_DIR" "$PROJECT_ROOT" "$LIST" > "$FILES" <<'EOF'
import json, os, sys
build, root, listing = sys.argv[1], os.path.realpath(sys.argv[2]), sys.argv[3] == "true"
# NOTE: the host build compiles a source into several test executables with the same
# flags; one entry per source keeps clang-tidy from checking it once per executable.
compiled = {}
for e in json.load(open(os.path.join(build, "compile_commands.json"))):
    compiled.setdefault(os.path.realpath(os.path.join(e["directory"], e["file"])), e)
ours = set()
for top in ("components", "apps", "main"):
    for d, _, names in os.walk(os.path.join(root, top)):
        ours.update(os.path.join(d, n) for n in names if n.endswith(".c"))
checked = sorted(ours & compiled.keys())
with open(os.path.join(build, "db", "compile_commands.json"), "w") as out:
    json.dump([dict(compiled[p], file=p) for p in checked], out, indent=1)
if listing:
    rel = lambda p: os.path.relpath(p, root)
    print("checked: %d" % len(checked))
    print("\n".join("  " + rel(p) for p in checked))
    rest = sorted(ours - compiled.keys())
    print("not compiled by the host build, not checked: %d" % len(rest))
    print("\n".join("  " + rel(p) for p in rest))
else:
    print("\n".join(checked))
EOF

if [[ "$LIST" == "true" ]]; then
    cat "$FILES"
    exit 0
fi

# NOTE: analyse with signed `char`. The target (Xtensa) and the CI host (x86_64) both have a
# signed char, so bugprone-narrowing-conversions must see int->char narrowing as it does there;
# without this an arm64 host (unsigned char, e.g. an Apple-silicon Mac) silently misses those
# findings and disagrees with CI.
echo "clang-tidy ($("$TIDY" --version | grep -o 'version [0-9.]*')) over $(wc -l < "$FILES" | tr -d ' ') sources..."
if xargs -P "$JOBS" -n 1 "$TIDY" -p "$BUILD_DIR/db" --quiet --extra-arg=-fsigned-char < "$FILES"; then
    echo "clang-tidy: no findings"
else
    echo "clang-tidy: findings above (see .clang-tidy for the policy)" >&2
    exit 1
fi
