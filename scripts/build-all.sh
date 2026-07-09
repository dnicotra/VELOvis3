#!/usr/bin/env bash
# Build both native (desktop) and Emscripten (web) targets — macOS / Linux
# The web build requires emsdk to be activated:
#   source /path/to/emsdk/emsdk_env.sh
# Usage:  ./scripts/build-all.sh
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"

echo "=== Building desktop (native) ==="
"$root/scripts/build-desktop.sh"

echo "=== Building web (Emscripten) ==="
"$root/scripts/build-web.sh"

echo "=== Done ==="
echo "Desktop exe: build-desktop/VELOvis3"
echo "Web output:  build-web/VELOvis3.html (serve with: cd build-web && python3 -m http.server 8080)"
