#include "config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <system_error>

#include <nlohmann/json.hpp>

namespace backdrop {
namespace {

std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

bool is_image_suffix(const fs::path& path) {
  static const std::set<std::string> kSuffixes = {
      ".jpg", ".jpeg", ".png", ".webp", ".bmp", ".gif", ".jxl", ".svg"};
  return kSuffixes.count(to_lower(path.extension().string())) > 0;
}

fs::path expand_user(const std::string& raw) {
  if (!raw.empty() && raw[0] == '~') {
    const char* home = std::getenv("HOME");
    if (home != nullptr) {
      return fs::path(home) / raw.substr(raw.size() > 1 && raw[1] == '/' ? 2 : 1);
    }
  }
  return fs::path(raw);
}

void append_unique_file(std::vector<fs::path>& files, std::set<fs::path>& seen, const fs::path& path) {
  std::error_code ec;
  if (!fs::is_regular_file(path, ec) || !is_image_suffix(path)) {
    return;
  }
  fs::path resolved = fs::weakly_canonical(path, ec);
  if (!ec && seen.insert(resolved).second) {
    files.push_back(resolved);
  }
}

}  // namespace

fs::path config_path() {
  const char* home = std::getenv("HOME");
  if (home == nullptr) {
    return fs::path(".config/backdrop/config.json");
  }
  return fs::path(home) / ".config" / "backdrop" / "config.json";
}

const ImportEntry* Config::find_import_by_hash(const std::string& sha256) const {
  if (sha256.empty()) {
    return nullptr;
  }
  for (const auto& entry : imports) {
    if (entry.sha256 == sha256) {
      return &entry;
    }
  }
  return nullptr;
}

const ImportEntry* Config::find_import_by_path(const std::string& path) const {
  for (const auto& entry : imports) {
    if (entry.path == path) {
      return &entry;
    }
  }
  return nullptr;
}

void Config::add_or_update_import(const std::string& path, const std::string& sha256) {
  for (auto& entry : imports) {
    if (entry.sha256 == sha256 || entry.path == path) {
      entry.path = path;
      entry.sha256 = sha256;
      return;
    }
  }
  imports.push_back(ImportEntry{path, sha256});
}

void Config::remove_path_references(const std::string& path) {
  paths.erase(std::remove(paths.begin(), paths.end(), path), paths.end());
  imports.erase(std::remove_if(imports.begin(), imports.end(),
                               [&](const ImportEntry& e) { return e.path == path; }),
                imports.end());
}

std::vector<fs::path> Config::image_files() const {
  std::vector<fs::path> files;
  std::set<fs::path> seen;

  for (const auto& raw : paths) {
    fs::path path = expand_user(raw);
    std::error_code ec;
    if (!fs::exists(path, ec)) {
      continue;
    }
    if (fs::is_regular_file(path, ec)) {
      append_unique_file(files, seen, path);
    } else if (fs::is_directory(path, ec)) {
      std::vector<fs::path> children;
      for (fs::recursive_directory_iterator it(path, ec), end; it != end; it.increment(ec)) {
        if (ec) {
          ec.clear();
          continue;
        }
        if (it->is_regular_file(ec) && is_image_suffix(it->path())) {
          children.push_back(it->path());
        }
      }
      std::sort(children.begin(), children.end());
      for (const auto& child : children) {
        append_unique_file(files, seen, child);
      }
    }
  }

  for (const auto& entry : imports) {
    append_unique_file(files, seen, expand_user(entry.path));
  }

  return files;
}

void Config::save() const {
  const fs::path path = config_path();
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);

  nlohmann::json j;
  j["paths"] = paths;
  j["imports"] = nlohmann::json::array();
  for (const auto& entry : imports) {
    j["imports"].push_back({{"path", entry.path}, {"sha256", entry.sha256}});
  }
  j["interval_minutes"] = interval_minutes;
  j["shuffle"] = shuffle;
  j["running"] = running;

  std::ofstream out(path);
  out << j.dump(2) << '\n';
}

Config Config::load() {
  Config cfg;
  const fs::path path = config_path();
  std::ifstream in(path);
  if (!in) {
    return cfg;
  }
  try {
    nlohmann::json j;
    in >> j;
    if (j.contains("paths") && j["paths"].is_array()) {
      cfg.paths = j["paths"].get<std::vector<std::string>>();
    }
    if (j.contains("imports") && j["imports"].is_array()) {
      for (const auto& item : j["imports"]) {
        ImportEntry entry;
        if (item.contains("path")) {
          entry.path = item["path"].get<std::string>();
        }
        if (item.contains("sha256")) {
          entry.sha256 = item["sha256"].get<std::string>();
        }
        if (!entry.path.empty()) {
          cfg.imports.push_back(std::move(entry));
        }
      }
    }
    if (j.contains("interval_minutes")) {
      cfg.interval_minutes = j["interval_minutes"].get<double>();
      if (cfg.interval_minutes <= 0.0) {
        cfg.interval_minutes = 30.0;
      }
    }
    if (j.contains("shuffle")) {
      cfg.shuffle = j["shuffle"].get<bool>();
    }
    if (j.contains("running")) {
      cfg.running = j["running"].get<bool>();
    }
  } catch (...) {
    return Config{};
  }
  return cfg;
}

}  // namespace backdrop
