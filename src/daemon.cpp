#include "daemon.hpp"

#include "config.hpp"
#include "rotator.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>
#include "i18n.hpp"

namespace backdrop {
namespace {

std::atomic<bool> g_stop{false};
std::atomic<bool> g_next{false};
std::atomic<bool> g_reload{false};

void handle_signal(int sig) {
  if (sig == SIGUSR1) {
    g_next = true;
  } else if (sig == SIGHUP) {
    g_reload = true;
  } else {
    g_stop = true;
  }
}

}  // namespace

int run_daemon() {
  Config config = Config::load();
  if (config.image_files().empty()) {
    std::cerr << _("No images in config. Set them up in the GUI first.\n");
    return 1;
  }

  g_stop = false;
  g_next = false;
  g_reload = false;
  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);
  std::signal(SIGUSR1, handle_signal);
  std::signal(SIGHUP, handle_signal);

  Rotator rotator(
      [](const fs::path& p) { std::cout << "wallpaper: " << p.string() << '\n'; },
      [](const std::string& m) { std::cerr << m << '\n'; });

  if (!rotator.start(config)) {
    return 1;
  }

  config.running = true;
  config.save();
  std::cout << "Backdrop daemon: " << config.image_files().size() << " images, every "
            << config.interval_minutes << " min\n";
  std::cout << "Signals: SIGUSR1=next, SIGHUP=reload, SIGTERM=stop\n";

  while (!g_stop) {
    if (g_reload.exchange(false)) {
      Config reloaded = Config::load();
      reloaded.running = true;
      if (reloaded.image_files().empty()) {
        std::cerr << _("Reload: no images in config, stopping.\n");
        g_stop = true;
        break;
      }
      rotator.configure(reloaded);
      config = std::move(reloaded);
      config.save();
      std::cout << "Reloaded config: " << config.image_files().size() << " images, every "
                << config.interval_minutes << " min\n";
    }
    if (g_next.exchange(false)) {
      rotator.next();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  rotator.stop();
  config.running = false;
  config.save();
  return 0;
}

}  // namespace backdrop
