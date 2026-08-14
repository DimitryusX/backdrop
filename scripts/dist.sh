#!/usr/bin/env bash
# Create source tarball: backdrop-VERSION.tar.gz (Source0 for RPM).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(grep -E '^\s*project\(backdrop VERSION' "$ROOT/CMakeLists.txt" \
  | sed -E 's/.*VERSION ([0-9.]+).*/\1/')"
NAME="backdrop"
OUT_DIR="${OUT_DIR:-$ROOT/dist}"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

mkdir -p "$OUT_DIR" "$STAGE/$NAME-$VERSION"

tar -C "$ROOT" \
  --exclude='build' \
  --exclude='dist' \
  --exclude='.git' \
  --exclude='.vscode' \
  --exclude='.flatpak-builder' \
  --exclude='*.rpm' \
  --exclude='*.src.rpm' \
  -cf - . | tar -C "$STAGE/$NAME-$VERSION" -xf -

ARCHIVE="$OUT_DIR/$NAME-$VERSION.tar.gz"
tar -C "$STAGE" -czf "$ARCHIVE" "$NAME-$VERSION"

echo "Source archive: $ARCHIVE"
echo "Version:        $VERSION"
