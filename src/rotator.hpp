#pragma once

#include "config.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace backdrop {

class Rotator {
 public:
  using ChangeCb = std::function<void(const fs::path&)>;
  using ErrorCb = std::function<void(const std::string&)>;

  Rotator(ChangeCb on_change = {}, ErrorCb on_error = {});
  ~Rotator();

  Rotator(const Rotator&) = delete;
  Rotator& operator=(const Rotator&) = delete;

  [[nodiscard]] bool running() const;
  void configure(const Config& config);
  bool start(const Config& config, bool apply_now = true);
  void stop();
  void next();

 private:
  void worker_loop();
  void apply_current();

  ChangeCb on_change_;
  ErrorCb on_error_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::thread worker_;
  std::vector<fs::path> images_;
  std::size_t index_ = 0;
  double interval_seconds_ = 1800.0;
  bool shuffle_ = true;
  bool running_ = false;
  bool stop_requested_ = false;
  bool wake_ = false;
};

}  // namespace backdrop
