#include "rotator.hpp"

#include "i18n.hpp"
#include "wallpaper.hpp"

#include <algorithm>
#include <chrono>
#include <random>

namespace backdrop {

Rotator::Rotator(ChangeCb on_change, ErrorCb on_error)
    : on_change_(std::move(on_change)), on_error_(std::move(on_error)) {}

Rotator::~Rotator() { stop(); }

bool Rotator::running() const {
  std::lock_guard lock(mutex_);
  return running_;
}

void Rotator::configure(const Config& config) {
  std::lock_guard lock(mutex_);
  images_ = config.image_files();
  if (config.shuffle && !images_.empty()) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(images_.begin(), images_.end(), gen);
  }
  interval_seconds_ = std::max(10.0, config.interval_minutes * 60.0);
  shuffle_ = config.shuffle;
  index_ = 0;
}

bool Rotator::start(const Config& config, bool apply_now) {
  configure(config);

  {
    std::lock_guard lock(mutex_);
    if (images_.empty()) {
      if (on_error_) {
        on_error_(_("No images. Add files or a folder."));
      }
      return false;
    }
    if (running_) {
      wake_ = true;
      cv_.notify_all();
      return true;
    }
    running_ = true;
    stop_requested_ = false;
  }

  if (apply_now) {
    apply_current();
  }

  if (worker_.joinable()) {
    worker_.join();
  }
  worker_ = std::thread([this] { worker_loop(); });
  return true;
}

void Rotator::stop() {
  {
    std::lock_guard lock(mutex_);
    if (!running_ && !worker_.joinable()) {
      return;
    }
    running_ = false;
    stop_requested_ = true;
    wake_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
  std::lock_guard lock(mutex_);
  stop_requested_ = false;
  wake_ = false;
}

void Rotator::next() {
  {
    std::lock_guard lock(mutex_);
    if (images_.empty()) {
      return;
    }
    index_ = (index_ + 1) % images_.size();
    if (running_) {
      wake_ = true;
    }
  }
  apply_current();
  cv_.notify_all();
}

void Rotator::worker_loop() {
  while (true) {
    {
      std::unique_lock lock(mutex_);
      if (stop_requested_ || !running_) {
        running_ = false;
        return;
      }
      const double wait_seconds = interval_seconds_;
      wake_ = false;
      cv_.wait_for(lock, std::chrono::duration<double>(wait_seconds), [this] {
        return wake_ || stop_requested_ || !running_;
      });
      if (stop_requested_ || !running_) {
        running_ = false;
        return;
      }
      if (wake_) {
        continue;
      }
      if (images_.empty()) {
        continue;
      }
      index_ = (index_ + 1) % images_.size();
      if (shuffle_ && index_ == 0 && images_.size() > 1) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::shuffle(images_.begin(), images_.end(), gen);
      }
    }
    apply_current();
  }
}

void Rotator::apply_current() {
  fs::path path;
  {
    std::lock_guard lock(mutex_);
    if (images_.empty()) {
      return;
    }
    path = images_[index_ % images_.size()];
  }

  if (!set_wallpaper(path)) {
    if (on_error_) {
      on_error_(_("Failed to set: ") + path.filename().string());
    }
    return;
  }
  if (on_change_) {
    on_change_(path);
  }
}

}  // namespace backdrop
