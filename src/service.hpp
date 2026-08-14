#pragma once

#include <string>

namespace backdrop::service {

inline constexpr const char* kUnitName = "backdrop.service";

/** Absolute path of this running binary (/proc/self/exe). */
[[nodiscard]] std::string self_executable();

/** True if systemctl is available (on host when inside Flatpak). */
[[nodiscard]] bool available();

[[nodiscard]] bool is_active();
[[nodiscard]] bool is_enabled();

/**
 * Ensure a usable user unit exists.
 * Prefers a packaged unit under /usr(/local)/lib/systemd/user/.
 * Otherwise writes ~/.config/systemd/user/backdrop.service for exec_path.
 */
bool ensure_unit(const std::string& exec_path, std::string* error = nullptr);

/** enable --now (persist across reboot + start). */
bool enable_now(std::string* error = nullptr);

/** disable --now (stop + do not start after reboot). */
bool disable_now(std::string* error = nullptr);

/** Ask running daemon to apply next wallpaper (SIGUSR1). */
bool request_next(std::string* error = nullptr);

/** Ask running daemon to reload config (SIGHUP). */
bool reload_config(std::string* error = nullptr);

}  // namespace backdrop::service
