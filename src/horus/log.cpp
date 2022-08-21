#include "log.hpp"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace horus {

class logger::impl {
public:
  impl() noexcept = default;

  impl(impl&& other) = delete;
  impl(const impl& other) = delete;
  impl& operator=(impl&& other) = delete;
  impl& operator=(const impl& other) = delete;

  ~impl()
  {
    stop();
  }

  void start(std::string filename, bool write_to_stdout) noexcept
  {
    stop();

    filename_ = std::move(filename);
    write_to_stdout_ = write_to_stdout;

    std::error_code ec;
    std::filesystem::remove(filename_, ec);

    stop_.store(false, std::memory_order_release);
    thread_ = std::thread([this]() noexcept {
      std::unique_lock lock(mutex_);
      do {
        const auto queue = std::move(queue_);
        queue_.clear();
        if (!queue.empty()) {
          lock.unlock();
          write(queue);
          lock.lock();
        }
        cv_.wait(lock, [this]() {
          return !queue_.empty() || stop_.load(std::memory_order_acquire);
        });
      } while (!stop_.load(std::memory_order_relaxed));
      if (!queue_.empty()) {
        write(queue_);
      }
    });
  }

  void log(std::string text) noexcept
  {
    std::lock_guard lock(mutex_);
    if (write_to_stdout_) {
      std::puts(text.data());
      std::fflush(stdout);
    }
    queue_.emplace_back(std::move(text));
    cv_.notify_one();
  }

  static std::shared_ptr<impl> get() noexcept
  {
    static std::mutex mutex;
    static std::weak_ptr<impl> wp;
    if (const auto sp = wp.lock()) {
      return sp;
    }
    std::lock_guard lock(mutex);
    if (const auto sp = wp.lock()) {
      return sp;
    }
    const auto sp = std::make_shared<impl>();
    wp = sp;
    return sp;
  }

private:
  void stop() noexcept
  {
    if (thread_.joinable()) {
      stop_.store(true, std::memory_order_release);
      cv_.notify_one();
      thread_.join();
    }
  }

  void write(const std::vector<std::string>& queue) noexcept
  {
    if (auto os = std::ofstream(filename_, std::ios::app | std::ios::binary)) {
      for (const auto& e : queue) {
        os.write(e.data(), e.size());
        os.put('\n');
      }
    }
  }

  std::string filename_;
  bool write_to_stdout_ = true;

  std::thread thread_;
  std::atomic_bool stop_ = false;

  std::mutex mutex_;
  std::vector<std::string> queue_;
  std::condition_variable cv_;
};

logger::logger() noexcept : impl_(logger::impl::get()) {}

logger::logger(std::string filename, bool write_to_stdout) noexcept : impl_(logger::impl::get())
{
  impl_->start(std::move(filename), write_to_stdout);
}

void logger::log(std::string text) noexcept
{
  impl::get()->log(std::move(text));
}

}  // namespace horus