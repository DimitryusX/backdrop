#!/usr/bin/env bash
# Build and optionally install a Flatpak package.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MANIFEST="$ROOT/packaging/flatpak/io.nexol.Backdrop.yml"
STATE_DIR="${STATE_DIR:-/tmp/backdrop-fp-state}"
BUILD_DIR="${FLATPAK_BUILD_DIR:-/tmp/backdrop-fp}"
REPO_DIR="${FLATPAK_REPO_DIR:-$ROOT/dist/flatpak-repo}"
INSTALL="${INSTALL:-1}"

if ! command -v flatpak-builder >/dev/null 2>&1; then
  echo "flatpak-builder not found. Install: sudo dnf install flatpak-builder" >&2
  exit 1
fi

mkdir -p "$ROOT/dist"

ARGS=(
  --force-clean
  --state-dir "$STATE_DIR"
  --repo "$REPO_DIR"
)
if [[ "$INSTALL" == "1" ]]; then
  ARGS+=(--user --install)
fi

flatpak-builder "${ARGS[@]}" "$BUILD_DIR" "$MANIFEST"

echo
if [[ "$INSTALL" == "1" ]]; then
  echo "Installed. Run: flatpak run io.nexol.Backdrop"
else
  echo "Built into repo: $REPO_DIR"
  echo "Install later: flatpak --user remote-add --no-gpg-verify backdrop-local $REPO_DIR"
  echo "               flatpak --user install backdrop-local io.nexol.Backdrop"
fi
