#!/usr/bin/env bash
# Build Backdrop (Release by default).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" "$@"
cmake --build "$BUILD_DIR" -j"$JOBS"

echo
echo "Binary: $BUILD_DIR/backdrop"
echo "Run:    $BUILD_DIR/backdrop"
echo "Daemon: $BUILD_DIR/backdrop --daemon"
