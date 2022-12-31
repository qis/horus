#pragma once
#include <horus/eye.hpp>
#include <horus/hid.hpp>
#include <rock/client.hpp>

namespace horus::hero {

class hitscan {
public:
  using clock = std::chrono::high_resolution_clock;

  hitscan(eye& eye, rock::client& client) noexcept : eye_(eye), client_(client) {}

  void scan(std::uint8_t* data, const hid::keybd& keybd, const hid::mouse& mouse, clock::time_point frame) noexcept
  {
    using namespace std::chrono_literals;

    // Check if target is acquired.
    const auto target = eye_.scan(data, mouse.dx, mouse.dy);

    // Skip when not enabled.
    if (!enabled_) {
      return;
    }

    auto set = false;
    if (target && frame >= blocked_) {
      client_.lock(32ms);
      client_.mask(rock::button::up, 7ms);
      set = true;
    }

    if (mouse.left || set) {
      blocked_ = frame + 128ms;
    }
  }

  bool toggle() noexcept
  {
    const auto enabled = enabled_;
    enabled_ = !enabled_;
    return enabled;
  }

  bool enable() noexcept
  {
    const auto enabled = enabled_;
    enabled_ = true;
    return enabled;
  }

  bool disable() noexcept
  {
    const auto enabled = enabled_;
    enabled_ = false;
    return enabled;
  }

private:
  eye& eye_;
  rock::client& client_;

  bool enabled_{ true };
  clock::time_point blocked_{};
};

}  // namespace horus::hero