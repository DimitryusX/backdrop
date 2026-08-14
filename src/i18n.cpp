#include "i18n.hpp"

#include <cstdlib>
#include <filesystem>
#include <locale.h>
#include <string>
#include <system_error>

namespace backdrop {
namespace {

namespace fs = std::filesystem;

bool catalog_present(const fs::path& locale_root) {
  std::error_code ec;
  return fs::is_regular_file(locale_root / "uk" / "LC_MESSAGES" / (std::string(GETTEXT_PACKAGE) + ".mo"),
                             ec);
}

}  // namespace

void init_i18n() {
  setlocale(LC_ALL, "");

  const char* dir = nullptr;
  if (const char* env = std::getenv("BACKDROP_LOCALEDIR")) {
    dir = env;
  }
#ifdef BACKDROP_BUILD_LOCALEDIR
  else if (catalog_present(BACKDROP_BUILD_LOCALEDIR)) {
    dir = BACKDROP_BUILD_LOCALEDIR;
  }
#endif
#ifdef BACKDROP_LOCALEDIR
  else {
    dir = BACKDROP_LOCALEDIR;
  }
#endif

  if (dir != nullptr) {
    bindtextdomain(GETTEXT_PACKAGE, dir);
  }
  bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
  textdomain(GETTEXT_PACKAGE);
}

}  // namespace backdrop
