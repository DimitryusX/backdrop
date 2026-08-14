#include "service.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include "i18n.hpp"

namespace backdrop::service {
namespace {

namespace fs = std::filesystem;

bool in_flatpak() { return std::getenv("FLATPAK_ID") != nullptr; }

std::string home_dir() {
  if (const char* h = std::getenv("HOME")) {
    return h;
  }
  return {};
}

bool run_capture(const std::vector<std::string>& args, std::string* out, int* exit_code) {
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
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);
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
  std::string buf;
  char tmp[4096];
  ssize_t n;
  while ((n = read(pipefd[0], tmp, sizeof(tmp))) > 0) {
    buf.append(tmp, static_cast<size_t>(n));
  }
  close(pipefd[0]);

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    return false;
  }
  if (out != nullptr) {
    while (!buf.empty() && (buf.back() == '\n' || buf.back() == '\r')) {
      buf.pop_back();
    }
    *out = std::move(buf);
  }
  if (exit_code != nullptr) {
    *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  }
  return WIFEXITED(status);
}

bool systemctl(std::initializer_list<std::string> args, std::string* out = nullptr,
               int* code = nullptr) {
  std::vector<std::string> cmd;
  cmd.emplace_back("systemctl");
  cmd.emplace_back("--user");
  for (const auto& a : args) {
    cmd.push_back(a);
  }
  int ec = 1;
  const bool ran = run_capture(cmd, out, &ec);
  if (code != nullptr) {
    *code = ec;
  }
  return ran && ec == 0;
}

bool unit_file_exists(const fs::path& path) {
  std::error_code ec;
  return fs::is_regular_file(path, ec);
}

std::string unit_body(const std::string& exec_path) {
  std::ostringstream ss;
  ss << "[Unit]\n"
     << "Description=Backdrop wallpaper rotator\n"
     << "After=graphical-session.target\n"
     << "\n"
     << "[Service]\n"
     << "Type=simple\n"
     << "ExecStart=" << exec_path << " --daemon\n"
     << "Restart=on-failure\n"
     << "RestartSec=3\n"
     << "\n"
     << "[Install]\n"
     << "WantedBy=default.target\n";
  return ss.str();
}

bool packaged_unit_present() {
  static const std::array<const char*, 3> kPaths = {
      "/usr/lib/systemd/user/backdrop.service",
      "/usr/local/lib/systemd/user/backdrop.service",
      "/etc/systemd/user/backdrop.service",
  };
  for (const char* p : kPaths) {
    if (unit_file_exists(p)) {
      return true;
    }
  }
  return false;
}

}  // namespace

std::string self_executable() {
  std::error_code ec;
  fs::path exe = fs::read_symlink("/proc/self/exe", ec);
  if (ec) {
    return {};
  }
  return fs::weakly_canonical(exe, ec).string();
}

bool available() {
  int code = 1;
  std::string out;
  // `systemctl --user is-system-running` may return non-zero on degraded; just probe binary.
  run_capture({"systemctl", "--version"}, &out, &code);
  return code == 0 || !out.empty();
}

bool is_active() {
  return systemctl({"is-active", "--quiet", kUnitName});
}

bool is_enabled() {
  return systemctl({"is-enabled", "--quiet", kUnitName});
}

bool ensure_unit(const std::string& exec_path, std::string* error) {
  if (exec_path.empty()) {
    if (error != nullptr) {
      *error = _("Could not resolve backdrop executable path");
    }
    return false;
  }

  // Always keep a user unit with the absolute ExecStart we want.
  // This covers local builds and overrides a packaged unit's path when needed.
  const std::string home = home_dir();
  if (home.empty()) {
    if (error != nullptr) {
      *error = _("HOME is not set");
    }
    return false;
  }

  const fs::path dir = fs::path(home) / ".config" / "systemd" / "user";
  const fs::path unit = dir / kUnitName;
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    if (error != nullptr) {
      *error = _("Failed to create ") + dir.string();
    }
    return false;
  }

  const std::string body = unit_body(exec_path);
  bool need_write = true;
  if (unit_file_exists(unit)) {
    std::ifstream in(unit);
    std::string existing((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (existing == body) {
      need_write = false;
    }
  }
  if (need_write) {
    std::ofstream out(unit, std::ios::trunc);
    if (!out) {
      if (error != nullptr) {
        *error = _("Failed to write ") + unit.string();
      }
      return false;
    }
    out << body;
  }

  (void)packaged_unit_present();
  if (!systemctl({"daemon-reload"}, nullptr, nullptr) && error != nullptr) {
    // daemon-reload returning non-zero is unusual; still try enable.
  }
  return true;
}

bool enable_now(std::string* error) {
  std::string out;
  if (!systemctl({"enable", "--now", kUnitName}, &out)) {
    if (error != nullptr) {
      *error = out.empty() ? _("systemctl enable --now failed") : out;
    }
    return false;
  }
  return true;
}

bool disable_now(std::string* error) {
  std::string out;
  // disable --now stops and unlinks from default.target
  if (!systemctl({"disable", "--now", kUnitName}, &out)) {
    // If unit was never enabled, try stop alone.
    if (!systemctl({"stop", kUnitName}, &out)) {
      if (error != nullptr) {
        *error = out.empty() ? _("failed to stop service") : out;
      }
      return false;
    }
  }
  return true;
}

bool request_next(std::string* error) {
  std::string out;
  if (!systemctl({"kill", "-s", "USR1", kUnitName}, &out)) {
    if (error != nullptr) {
      *error = out.empty() ? _("failed to send SIGUSR1") : out;
    }
    return false;
  }
  return true;
}

bool reload_config(std::string* error) {
  std::string out;
  if (!systemctl({"kill", "-s", "HUP", kUnitName}, &out)) {
    if (error != nullptr) {
      *error = out.empty() ? _("failed to send SIGHUP") : out;
    }
    return false;
  }
  return true;
}

}  // namespace backdrop::service
