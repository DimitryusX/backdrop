#!/usr/bin/env bash
# Build and install Backdrop into PREFIX (default: /usr/local).
# Examples:
#   ./scripts/install.sh
#   PREFIX=/usr ./scripts/install.sh
#   DESTDIR=/tmp/stage PREFIX=/usr ./scripts/install.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-}"

"$ROOT/scripts/build.sh" -DCMAKE_INSTALL_PREFIX="$PREFIX"

if [[ -n "$DESTDIR" ]]; then
  DESTDIR="$DESTDIR" cmake --install "$BUILD_DIR" --prefix "$PREFIX"
  echo
  echo "Staged under: $DESTDIR$PREFIX"
else
  cmake --install "$BUILD_DIR" --prefix "$PREFIX"
  echo
  echo "Installed: $PREFIX/bin/backdrop"
fi

echo "Uninstall: sudo ./scripts/uninstall.sh"
echo "       or: sudo make uninstall"
