#!/usr/bin/env bash
# Web (Emscripten) build — macOS / Linux
# Requires emsdk to be activated:  source /path/to/emsdk/emsdk_env.sh
# Usage:  ./scripts/build-web.sh [--serve]
set -euo pipefail
cd "$(dirname "$0")/.."

if ! command -v emcmake >/dev/null 2>&1; then
    echo "error: emcmake not found. Activate emsdk first:" >&2
    echo "  source /path/to/emsdk/emsdk_env.sh" >&2
    exit 1
fi

emcmake cmake -B build-web -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build-web --parallel "$(nproc 2>/dev/null || sysctl -n hw.ncpu)"

if [[ "${1:-}" == "--serve" ]]; then
    echo "Serving on http://localhost:8080/VELOvis3.html"
    (cd build-web && python3 -m http.server 8080)
fi
