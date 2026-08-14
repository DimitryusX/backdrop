#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace backdrop {

namespace fs = std::filesystem;

struct ImportEntry {
  std::string path;
  std::string sha256;
};

struct Config {
  /** Folder links (and legacy loose file paths). */
  std::vector<std::string> paths;
  /** Imported copies with content hashes (checked only on import). */
  std::vector<ImportEntry> imports;
  double interval_minutes = 30.0;
  bool shuffle = true;
  bool running = false;

  [[nodiscard]] std::vector<fs::path> image_files() const;
  [[nodiscard]] const ImportEntry* find_import_by_hash(const std::string& sha256) const;
  [[nodiscard]] const ImportEntry* find_import_by_path(const std::string& path) const;
  void add_or_update_import(const std::string& path, const std::string& sha256);
  void remove_path_references(const std::string& path);

  void save() const;
  static Config load();
};

fs::path config_path();

}  // namespace backdrop
