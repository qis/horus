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
    if (!enabled_ || mouse.right) {
      return;
    }

    auto set = false;
    if (target && frame >= blocked_ && target->distance < 64.0) {
      // Center position.
      constexpr auto cw = static_cast<int>(eye::sw / 2);
      constexpr auto ch = static_cast<int>(eye::sh / 2);

      // Mouse position.
      const auto mx = cw + mouse.dx;
      const auto my = ch + mouse.dy;

      // Target to mouse offset.
      const auto dx = target->point.x - mx;
      const auto dy = target->point.y - my;

      // Check if the point is close enough on the x axis.
      if (const auto ax = std::abs(dx); ax < 16 || ax < target->cw / 2) {
        client_.lock(32ms);
        client_.move(1, static_cast<int16_t>(dx * 5), static_cast<int16_t>(dy * 4));
        client_.mask(rock::button::up, 7ms);
        set = true;
      }
    }

    if (mouse.left || set) {
      blocked_ = frame + 525ms;
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