#pragma once

#include "config.hpp"

#include <filesystem>
#include <string>

namespace backdrop {

namespace fs = std::filesystem;

/** ~/.local/share/backdrop (or $XDG_DATA_HOME/backdrop) */
[[nodiscard]] fs::path data_dir();

/** Imported wallpaper copies live here. */
[[nodiscard]] fs::path wallpapers_dir();

/** True if path is inside wallpapers_dir() (managed copy). */
[[nodiscard]] bool is_managed_path(const fs::path& path);

/** SHA-256 hex digest of a file (streamed). Empty on failure. */
[[nodiscard]] std::string file_sha256(const fs::path& path, std::string* error = nullptr);

struct ImportResult {
  fs::path path;
  std::string sha256;
  bool duplicate = false;
};

/**
 * Import an image into wallpapers_dir() unless the same SHA-256 already exists in config.
 * On duplicate, returns the existing import path and duplicate=true (no new copy).
 */
[[nodiscard]] ImportResult import_image(const fs::path& source, const Config& config,
                                        std::string* error = nullptr);

/** Delete a managed file (no-op if path is not managed). */
void remove_managed_path(const fs::path& path);

/** Delete the entire ~/.local/share/backdrop tree (imported wallpapers). */
void purge_data_dir();

}  // namespace backdrop
