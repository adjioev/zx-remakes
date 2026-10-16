#!/usr/bin/env bash
# Build (if needed) and start ZX Remakes.
#   ./run.sh            build + play
#   ./run.sh --rebuild  wipe the build dir first
# Other arguments go to the program (e.g. a game file, or --test <dir> --game ballbreaker "...").
set -euo pipefail
cd "$(dirname "$0")/remaster"

if ! command -v cmake >/dev/null || ! pkg-config --exists raylib 2>/dev/null; then
    echo "Installing dependencies (cmake, raylib) via Homebrew..."
    brew install cmake pkgconf raylib
fi

if [[ "${1:-}" == "--rebuild" ]]; then
    rm -rf build
    shift
fi

[[ -f build/CMakeCache.txt ]] || cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
exec ./build/zxremakes "$@"
