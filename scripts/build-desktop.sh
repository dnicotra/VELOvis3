#!/usr/bin/env bash
# Desktop (native) build — macOS / Linux
# Usage:  ./scripts/build-desktop.sh [--run]
set -euo pipefail
cd "$(dirname "$0")/.."

cmake -B build-desktop -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build-desktop --config Release --parallel "$(nproc 2>/dev/null || sysctl -n hw.ncpu)"

if [[ "${1:-}" == "--run" ]]; then
    exe="$(find build-desktop -type f -name VELOvis3 -perm -u+x 2>/dev/null | head -n1)"
    if [[ -n "$exe" ]]; then
        "$exe"
    else
        echo "error: executable not found" >&2
        exit 1
    fi
fi
