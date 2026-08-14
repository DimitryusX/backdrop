#include "wallpaper.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fcntl.h>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace backdrop {
namespace {

std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

bool in_flatpak() {
  return std::getenv("FLATPAK_ID") != nullptr;
}

std::string which(const std::string& name) {
  const char* path_env = std::getenv("PATH");
  if (path_env == nullptr) {
    return {};
  }
  std::stringstream ss(path_env);
  std::string dir;
  while (std::getline(ss, dir, ':')) {
    if (dir.empty()) {
      continue;
    }
    fs::path candidate = fs::path(dir) / name;
    if (access(candidate.c_str(), X_OK) == 0) {
      return candidate.string();
    }
  }
  return {};
}

std::string desktop() {
  if (const char* v = std::getenv("XDG_CURRENT_DESKTOP")) {
    return to_lower(v);
  }
  if (const char* v = std::getenv("DESKTOP_SESSION")) {
    return to_lower(v);
  }
  return {};
}

std::string file_uri(const fs::path& path) {
  return std::string("file://") + path.string();
}

bool run_argv(const std::vector<std::string>& args, bool background) {
  if (args.empty()) {
    return false;
  }

  std::vector<std::string> final_args;
  if (in_flatpak()) {
    final_args.push_back("flatpak-spawn");
    final_args.push_back("--host");
    final_args.insert(final_args.end(), args.begin(), args.end());
  } else {
    final_args = args;
  }

  std::vector<char*> argv;
  argv.reserve(final_args.size() + 1);
  for (auto& a : final_args) {
    argv.push_back(a.data());
  }
  argv.push_back(nullptr);

  pid_t pid = fork();
  if (pid < 0) {
    return false;
  }
  if (pid == 0) {
    if (background) {
      setsid();
    }
    int null_fd = open("/dev/null", O_RDWR);
    if (null_fd >= 0) {
      dup2(null_fd, STDIN_FILENO);
      dup2(null_fd, STDOUT_FILENO);
      dup2(null_fd, STDERR_FILENO);
      if (null_fd > STDERR_FILENO) {
        close(null_fd);
      }
    }
    execvp(argv[0], argv.data());
    _exit(127);
  }

  if (background) {
    return true;
  }

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    return false;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool run(std::initializer_list<std::string> args) {
  return run_argv(std::vector<std::string>(args), false);
}

bool run_bg(std::initializer_list<std::string> args) {
  return run_argv(std::vector<std::string>(args), true);
}

bool run_capture(const std::vector<std::string>& args, std::string& out) {
  std::vector<std::string> final_args;
  if (in_flatpak()) {
    final_args.push_back("flatpak-spawn");
    final_args.push_back("--host");
    final_args.insert(final_args.end(), args.begin(), args.end());
  } else {
    final_args = args;
  }

  int pipefd[2];
  if (pipe(pipefd) != 0) {
    return false;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return false;
  }
  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);
    int null_fd = open("/dev/null", O_RDWR);
    if (null_fd >= 0) {
      dup2(null_fd, STDERR_FILENO);
      if (null_fd > STDERR_FILENO) {
        close(null_fd);
      }
    }
    std::vector<char*> argv;
    argv.reserve(final_args.size() + 1);
    for (auto& a : final_args) {
      argv.push_back(a.data());
    }
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }

  close(pipefd[1]);
  out.clear();
  char buf[4096];
  ssize_t n;
  while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
    out.append(buf, static_cast<size_t>(n));
  }
  close(pipefd[0]);

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    return false;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool set_gsettings(const std::string& schema, const std::string& value,
                   const std::string& key = "picture-uri") {
  if (which("gsettings").empty() && !in_flatpak()) {
    return false;
  }
  bool ok = run({"gsettings", "set", schema, key, value});
  if (ok && key == "picture-uri") {
    run({"gsettings", "set", schema, "picture-uri-dark", value});
  }
  return ok;
}

bool set_xfce(const std::string& path) {
  if (which("xfconf-query").empty() && !in_flatpak()) {
    return false;
  }
  std::string listed;
  if (!run_capture({"xfconf-query", "--channel", "xfce4-desktop", "--list"}, listed)) {
    return false;
  }
  bool ok = false;
  std::stringstream ss(listed);
  std::string line;
  while (std::getline(ss, line)) {
    if (line.size() >= 11 && line.substr(line.size() - 11) == "/last-image") {
      if (run({"xfconf-query", "--channel", "xfce4-desktop", "--property", line, "--set", path})) {
        ok = true;
      }
    }
  }
  return ok;
}

bool set_plasma(const std::string& path) {
  if (!which("plasma-apply-wallpaperimage").empty() || in_flatpak()) {
    if (run({"plasma-apply-wallpaperimage", path})) {
      return true;
    }
  }

  std::string script =
      "var allDesktops = desktops();\n"
      "for (var i = 0; i < allDesktops.length; i++) {\n"
      "    d = allDesktops[i];\n"
      "    d.wallpaperPlugin = \"org.kde.image\";\n"
      "    d.currentConfigGroup = Array(\"Wallpaper\", \"org.kde.image\", \"General\");\n"
      "    d.writeConfig(\"Image\", \"file://" +
      path +
      "\");\n"
      "}\n";

  for (const char* bus : {"qdbus", "qdbus6"}) {
    if (!which(bus).empty() || in_flatpak()) {
      if (run({bus, "org.kde.plasmashell", "/PlasmaShell",
               "org.kde.PlasmaShell.evaluateScript", script})) {
        return true;
      }
    }
  }
  return false;
}

void kill_proc(const std::string& name) {
  run({"pkill", "-x", name});
}

}  // namespace

bool set_wallpaper(const fs::path& path_in) {
  std::error_code ec;
  fs::path path = fs::weakly_canonical(path_in, ec);
  if (ec || !fs::is_regular_file(path, ec)) {
    return false;
  }

  const std::string desk = desktop();
  const std::string uri = file_uri(path);
  const std::string str_path = path.string();

  if (desk.find("gnome") != std::string::npos || desk.find("unity") != std::string::npos ||
      desk.find("pantheon") != std::string::npos) {
    if (set_gsettings("org.gnome.desktop.background", uri)) {
      return true;
    }
  }

  if (desk.find("cinnamon") != std::string::npos) {
    if (set_gsettings("org.cinnamon.desktop.background", uri)) {
      return true;
    }
  }

  if (desk.find("mate") != std::string::npos) {
    if (set_gsettings("org.mate.background", str_path, "picture-filename")) {
      return true;
    }
  }

  if (desk.find("xfce") != std::string::npos) {
    if (set_xfce(str_path)) {
      return true;
    }
  }

  if (desk.find("kde") != std::string::npos || desk.find("plasma") != std::string::npos) {
    if (set_plasma(str_path)) {
      return true;
    }
  }

  if (desk.find("lxqt") != std::string::npos &&
      (!which("pcmanfm-qt").empty() || in_flatpak())) {
    if (run({"pcmanfm-qt", "--set-wallpaper", str_path})) {
      return true;
    }
  }

  if (desk.find("lxde") != std::string::npos && (!which("pcmanfm").empty() || in_flatpak())) {
    if (run({"pcmanfm", "--set-wallpaper", str_path})) {
      return true;
    }
  }

  if (std::getenv("SWAYSOCK") != nullptr && (!which("swaymsg").empty() || in_flatpak())) {
    if (run({"swaymsg", "output", "*", "bg", str_path, "fill"})) {
      return true;
    }
  }

  if (std::getenv("HYPRLAND_INSTANCE_SIGNATURE") != nullptr) {
    if (!which("hyprctl").empty() || in_flatpak()) {
      if (run({"hyprctl", "hyprpaper", "wallpaper", "," + str_path})) {
        return true;
      }
    }
    if (!which("swaybg").empty() || in_flatpak()) {
      kill_proc("swaybg");
      return run_bg({"swaybg", "-i", str_path, "-m", "fill"});
    }
  }

  if (!which("gsettings").empty() || in_flatpak()) {
    if (set_gsettings("org.gnome.desktop.background", uri)) {
      return true;
    }
    if (set_gsettings("org.cinnamon.desktop.background", uri)) {
      return true;
    }
  }

  if (!which("feh").empty() || in_flatpak()) {
    if (run({"feh", "--bg-fill", str_path})) {
      return true;
    }
  }

  if (!which("nitrogen").empty() || in_flatpak()) {
    if (run({"nitrogen", "--set-zoom-fill", "--save", str_path})) {
      return true;
    }
  }

  if (!which("swaybg").empty() || in_flatpak()) {
    kill_proc("swaybg");
    if (run_bg({"swaybg", "-i", str_path, "-m", "fill"})) {
      return true;
    }
  }

  if (!which("hsetroot").empty() || in_flatpak()) {
    if (run({"hsetroot", "-cover", str_path})) {
      return true;
    }
  }

  if (!which("xwallpaper").empty() || in_flatpak()) {
    if (run({"xwallpaper", "--zoom", str_path})) {
      return true;
    }
  }

  return false;
}

}  // namespace backdrop
