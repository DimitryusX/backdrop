#include "app.hpp"
#include "daemon.hpp"
#include "i18n.hpp"

#include <cstring>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  backdrop::init_i18n();

  bool daemon = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--daemon") == 0) {
      daemon = true;
      break;
    }
    if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
      std::cout << _("Backdrop — minimal Linux wallpaper rotator") << '\n'
                << _("Usage: backdrop [--daemon]") << '\n';
      return 0;
    }
  }

  if (daemon) {
    return backdrop::run_daemon();
  }
  return backdrop::run_ui(argc, argv);
}
