#!/usr/bin/env bash
# Uninstall Backdrop files installed via cmake / scripts/install.sh.
#
# Prefers the CMake install manifest (exact files from last install).
# Falls back to removing the known layout under PREFIX.
#
# Examples:
#   sudo ./scripts/uninstall.sh
#   sudo PREFIX=/usr ./scripts/uninstall.sh
#
# Packages installed via RPM/Flatpak should be removed with the package manager:
#   sudo dnf remove backdrop
#   flatpak uninstall io.nexol.Backdrop
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
PREFIX="${PREFIX:-/usr/local}"
MANIFEST="${BUILD_DIR}/install_manifest.txt"

removed=0

remove_path() {
  local path="$1"
  if [[ -e "$path" || -L "$path" ]]; then
    rm -f "$path"
    echo "removed: $path"
    removed=$((removed + 1))
  fi
}

stop_user_service() {
  if ! command -v systemctl >/dev/null 2>&1; then
    return 0
  fi

  run_user() {
    if [[ -n "${SUDO_USER:-}" && "${SUDO_USER}" != root && "$(id -u)" -eq 0 ]]; then
      sudo -u "$SUDO_USER" XDG_RUNTIME_DIR="/run/user/$(id -u "$SUDO_USER")" "$@"
    else
      "$@"
    fi
  }

  run_user systemctl --user disable --now backdrop.service >/dev/null 2>&1 || true

  local cfg_home="${HOME}"
  if [[ -n "${SUDO_USER:-}" && "${SUDO_USER}" != root && "$(id -u)" -eq 0 ]]; then
    cfg_home="$(getent passwd "$SUDO_USER" | cut -d: -f6)"
  fi
  local user_unit="${cfg_home}/.config/systemd/user/backdrop.service"
  if [[ -f "$user_unit" ]]; then
    rm -f "$user_unit"
    echo "removed: $user_unit"
    run_user systemctl --user daemon-reload >/dev/null 2>&1 || true
  fi
}

remove_user_data() {
  local cfg_home="${HOME}"
  if [[ -n "${SUDO_USER:-}" && "${SUDO_USER}" != root && "$(id -u)" -eq 0 ]]; then
    cfg_home="$(getent passwd "$SUDO_USER" | cut -d: -f6)"
  fi

  local data_dir="${cfg_home}/.local/share/backdrop"
  if [[ -d "$data_dir" ]]; then
    rm -rf "$data_dir"
    echo "removed: $data_dir"
    removed=$((removed + 1))
  fi

  local config_dir="${cfg_home}/.config/backdrop"
  if [[ -d "$config_dir" ]]; then
    rm -rf "$config_dir"
    echo "removed: $config_dir"
    removed=$((removed + 1))
  fi
}

refresh_caches() {
  local apps="${PREFIX}/share/applications"
  local icons="${PREFIX}/share/icons/hicolor"
  if command -v update-desktop-database >/dev/null 2>&1 && [[ -d "$apps" ]]; then
    update-desktop-database "$apps" >/dev/null 2>&1 || true
  fi
  if command -v gtk-update-icon-cache >/dev/null 2>&1 && [[ -d "$icons" ]]; then
    gtk-update-icon-cache -f -t "$icons" >/dev/null 2>&1 || true
  fi
}

stop_user_service
remove_user_data

if [[ -f "$MANIFEST" ]]; then
  echo "Using install manifest: $MANIFEST"
  while IFS= read -r file || [[ -n "$file" ]]; do
    [[ -z "$file" ]] && continue
    remove_path "$file"
  done < "$MANIFEST"
elif [[ -d "$BUILD_DIR" ]] && cmake --build "$BUILD_DIR" --target help 2>/dev/null | grep -q '^uninstall$'; then
  echo "Running CMake uninstall target in $BUILD_DIR"
  cmake --build "$BUILD_DIR" --target uninstall
  removed=1
else
  echo "No install manifest found — removing known files under PREFIX=$PREFIX"
  remove_path "${PREFIX}/bin/backdrop"
  remove_path "${PREFIX}/share/applications/io.nexol.Backdrop.desktop"
  remove_path "${PREFIX}/share/metainfo/io.nexol.Backdrop.metainfo.xml"
  remove_path "${PREFIX}/share/icons/hicolor/scalable/apps/io.nexol.Backdrop.svg"
  remove_path "${PREFIX}/lib/systemd/user/backdrop.service"
fi

refresh_caches

if [[ "$removed" -eq 0 ]]; then
  echo "Nothing to remove (already uninstalled, or wrong PREFIX?)."
  echo "Try: PREFIX=/usr $0"
  exit 1
fi

echo "Backdrop uninstalled."
