#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace backdrop {

namespace fs = std::filesystem;

bool set_wallpaper(const fs::path& path);

}  // namespace backdrop
