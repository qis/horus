#pragma once
#include <horus/eye.hpp>
#include <horus/hid.hpp>
#include <rock/client.hpp>

namespace horus::hero {

enum status : std::uint8_t {
  none = 0x00,
  draw = 0x01,
  beep = 0x02,
};

class hitscan {
public:
  using clock = std::chrono::high_resolution_clock;

  hitscan(eye& eye, rock::client& client) noexcept : eye_(eye), client_(client) {}

  status scan(
    std::uint8_t* data,
    const hid::keybd& keybd,
    const hid::mouse& mouse,
    clock::time_point frame) noexcept
  {
    // Disable enter, mouse down and shift.
    const auto shift_state = shift_state_;
    shift_state_ = keybd.shift;
    if (keybd.enter || mouse.down || (!shift_state && shift_state_)) {
      enabled_ = false;
      return status::draw;
    }

    // Enable on menu and left click.
    if (!enabled_) {
      if (keybd.menu || mouse.left) {
        enabled_ = true;
        if (mouse.left) {
          hitscan_blocked_ = frame + std::chrono::milliseconds(128);
        }
      }
      return status::draw;
    }

    // Check if target is acquired.
    const auto target = eye_.scan(data, mouse.dx, mouse.dy);

    // Shoot and prolong enabled time.
    auto set = false;
    if (frame >= hitscan_blocked_ && target) {
      client_.lock(std::chrono::milliseconds(18));
      client_.mask(rock::button::left, std::chrono::milliseconds(7));
      set = true;
    }

    // Block to prevent left mouse button spam.
    if (mouse.left || set) {
      hitscan_blocked_ = frame + std::chrono::milliseconds(128);
    }

    // Beep when left mouse button was pressed automatically.
    return set ? static_cast<status>(status::draw | status::beep) : status::draw;
  }

private:
  eye& eye_;
  rock::client& client_;
  clock::time_point hitscan_blocked_{ clock::now() };
  bool shift_state_{ false };
  bool enabled_{ false };
};

}  // namespace hero