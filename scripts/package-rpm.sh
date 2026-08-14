#!/usr/bin/env bash
# Build an RPM package (Fedora / RHEL-style rpmbuild).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(grep -E '^\s*project\(backdrop VERSION' "$ROOT/CMakeLists.txt" \
  | sed -E 's/.*VERSION ([0-9.]+).*/\1/')"
NAME="backdrop"
SPEC="$ROOT/packaging/rpm/backdrop.spec"

if ! command -v rpmbuild >/dev/null 2>&1; then
  echo "rpmbuild not found. Install: sudo dnf install rpm-build rpmdevtools" >&2
  exit 1
fi

# Ensure standard rpmbuild layout exists.
rpmdev-setuptree 2>/dev/null || mkdir -p "$HOME/rpmbuild"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

OUT_DIR="$ROOT/dist"
"$ROOT/scripts/dist.sh"
ARCHIVE="$OUT_DIR/$NAME-$VERSION.tar.gz"

cp -f "$ARCHIVE" "$HOME/rpmbuild/SOURCES/$NAME-$VERSION.tar.gz"
cp -f "$SPEC" "$HOME/rpmbuild/SPECS/backdrop.spec"

rpmbuild -ba "$HOME/rpmbuild/SPECS/backdrop.spec"

echo
echo "RPMs under: $HOME/rpmbuild/RPMS/"
find "$HOME/rpmbuild/RPMS" -name "${NAME}-${VERSION}*.rpm" -print
find "$HOME/rpmbuild/SRPMS" -name "${NAME}-${VERSION}*.src.rpm" -print 2>/dev/null || true

# Copy into project dist/ for convenience.
mkdir -p "$OUT_DIR"
find "$HOME/rpmbuild/RPMS" -name "${NAME}-${VERSION}*.rpm" -exec cp -f {} "$OUT_DIR/" \;
find "$HOME/rpmbuild/SRPMS" -name "${NAME}-${VERSION}*.src.rpm" -exec cp -f {} "$OUT_DIR/" \; 2>/dev/null || true
echo "Copied packages to: $OUT_DIR"
