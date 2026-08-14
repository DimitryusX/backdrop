#include "storage.hpp"

#include "i18n.hpp"

#include <cstdio>
#include <cstdlib>
#include <system_error>

#include <glib.h>

namespace backdrop {

fs::path data_dir() {
  if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && *xdg != '\0') {
    return fs::path(xdg) / "backdrop";
  }
  const char* home = std::getenv("HOME");
  if (home == nullptr) {
    return fs::path(".local/share/backdrop");
  }
  return fs::path(home) / ".local" / "share" / "backdrop";
}

fs::path wallpapers_dir() { return data_dir() / "wallpapers"; }

bool is_managed_path(const fs::path& path) {
  std::error_code ec_root;
  std::error_code ec_path;
  const fs::path root = fs::weakly_canonical(wallpapers_dir(), ec_root);
  const fs::path resolved = fs::weakly_canonical(path, ec_path);
  if (ec_root || ec_path || root.empty() || resolved.empty()) {
    const std::string r = wallpapers_dir().lexically_normal().string();
    const std::string p = path.lexically_normal().string();
    return p.size() >= r.size() && p.compare(0, r.size(), r) == 0 &&
           (p.size() == r.size() || p[r.size()] == '/');
  }
  auto root_it = root.begin();
  auto path_it = resolved.begin();
  for (; root_it != root.end() && path_it != resolved.end(); ++root_it, ++path_it) {
    if (*root_it != *path_it) {
      return false;
    }
  }
  return root_it == root.end();
}

std::string file_sha256(const fs::path& path, std::string* error) {
  FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    if (error != nullptr) {
      *error = _("Failed to read: ") + path.filename().string();
    }
    return {};
  }

  GChecksum* checksum = g_checksum_new(G_CHECKSUM_SHA256);
  unsigned char buf[8192];
  while (true) {
    const size_t n = std::fread(buf, 1, sizeof(buf), file);
    if (n > 0) {
      g_checksum_update(checksum, buf, static_cast<gssize>(n));
    }
    if (n < sizeof(buf)) {
      if (std::ferror(file)) {
        std::fclose(file);
        g_checksum_free(checksum);
        if (error != nullptr) {
          *error = _("Failed to read: ") + path.filename().string();
        }
        return {};
      }
      break;
    }
  }
  std::fclose(file);

  std::string hex = g_checksum_get_string(checksum);
  g_checksum_free(checksum);
  return hex;
}

ImportResult import_image(const fs::path& source, const Config& config, std::string* error) {
  ImportResult result;
  std::error_code ec;
  if (!fs::is_regular_file(source, ec)) {
    if (error != nullptr) {
      *error = _("Not a file: ") + source.string();
    }
    return result;
  }

  result.sha256 = file_sha256(source, error);
  if (result.sha256.empty()) {
    return result;
  }

  if (const ImportEntry* existing = config.find_import_by_hash(result.sha256)) {
    std::error_code exists_ec;
    if (fs::is_regular_file(existing->path, exists_ec)) {
      result.path = existing->path;
      result.duplicate = true;
      return result;
    }
    // Listed in config but missing on disk — fall through and re-import.
  }

  const fs::path dir = wallpapers_dir();
  fs::create_directories(dir, ec);
  if (ec) {
    if (error != nullptr) {
      *error = _("Failed to create ") + dir.string();
    }
    return {};
  }

  fs::path name = source.filename();
  if (name.empty()) {
    name = "wallpaper";
  }

  fs::path dest = dir / name;
  if (fs::exists(dest, ec)) {
    const auto stem = dest.stem().string();
    const auto ext = dest.extension().string();
    for (int i = 1; i < 10000; ++i) {
      dest = dir / (stem + "_" + std::to_string(i) + ext);
      if (!fs::exists(dest, ec)) {
        break;
      }
    }
  }

  fs::copy_file(source, dest, fs::copy_options::none, ec);
  if (ec) {
    if (error != nullptr) {
      *error = _("Failed to copy: ") + source.filename().string();
    }
    return {};
  }

  fs::path canonical = fs::weakly_canonical(dest, ec);
  result.path = ec ? dest : canonical;
  result.duplicate = false;
  return result;
}

void remove_managed_path(const fs::path& path) {
  if (!is_managed_path(path)) {
    return;
  }
  std::error_code ec;
  if (fs::is_regular_file(path, ec)) {
    fs::remove(path, ec);
  }
}

void purge_data_dir() {
  std::error_code ec;
  const fs::path root = data_dir();
  if (fs::exists(root, ec)) {
    fs::remove_all(root, ec);
  }
}

}  // namespace backdrop
