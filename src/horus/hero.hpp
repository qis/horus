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

    // Disable on enter.
    if (keybd.enter) {
      enabled_ = false;
      return;
    }

    // Enable on attack.
    if (!enabled_) {
      if (mouse.left || mouse.right) {
        enabled_ = true;
        if (mouse.left) {
          blocked_ = frame + 128ms;
        }
      }
      return;
    }

#if HERO_SUPPORT
    // Disable mouse down and shift.
    const auto shift_state = shift_state_;
    shift_state_ = keybd.shift;
    if (keybd.enter || mouse.down || (!shift_state && shift_state_)) {
      enabled_ = false;
      return;
    }
#else
    // How long it takes for the ammo value to change.
    constexpr auto ammo_interval = 104ms;

    // How long to wait between recoil compensation injects.
    constexpr auto compensate_interval = 16ms;

    // How much to move the mouse for each ammo value change.
    constexpr auto compensate_ammo = int16_t(48);

    // How often to move the mouse between ammo value changes.
    constexpr auto compensate_count = (ammo_interval - compensate_interval) / compensate_interval;

    // Mouse movement per inject.
    constexpr auto compensate_value = compensate_ammo / compensate_count;

    // Update state.
    const auto fire_state = fire_state_;
    fire_state_ = mouse.left;

    if (!fire_state_ || keybd.r) {
      compensate_ = false;
    } else if (!fire_state && fire_state_) {
      compensate_ammo_ = frame + ammo_interval * 30 + 200ms;
      compensate_next_ = frame + ammo_interval * 2;
      compensate_ = true;
    }

    if (compensate_ && fire_state_ && frame > compensate_next_) {
      if (frame > compensate_ammo_) {
        compensate_ = false;
      } else if (!mouse.right && copmensate_enabled_) {
        client_.move(1, 0, compensate_value);
      }
      compensate_next_ = frame + compensate_interval;
    }

    // Skip trigger bot when right mouse button is not pressed.
    if (!mouse.right) {
      return;
    }
#endif

    auto set = false;
    if (target && frame >= blocked_) {
      client_.lock(32ms);
      client_.mask(rock::button::up, 7ms);
      set = true;
    }

    // Block execution to prevent left mouse button spam.
    if (mouse.left || set) {
      blocked_ = frame + 128ms;
    }
  }

  bool toggle() noexcept
  {
    copmensate_enabled_ = !copmensate_enabled_;
    return copmensate_enabled_;
  }

private:
  eye& eye_;
  rock::client& client_;
  clock::time_point blocked_{};

  bool enabled_{ true };
  bool copmensate_enabled_{ true };

#if HERO_SUPPORT
  bool shift_state_{ false };
#else
  bool fire_state_{ false };
  bool compensate_{ false };
  clock::time_point compensate_next_{};
  clock::time_point compensate_ammo_{};
#endif
};

}  // namespace horus::hero