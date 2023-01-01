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

    // Update input states.
    const auto s_key = s_key_;
    s_key_ = keybd.s;

    const auto space_key = space_key_;
    space_key_ = keybd.space;

    const auto shift_key = shift_key_;
    shift_key_ = keybd.shift;

    // Skip when not enabled.
    if (!enabled_) {
      return;
    }

    // Required Bindings
    // ==============================
    // CROUCH | MOUSE 4 | LCONTROL
    // JUMP   | MOUSE 5 |

    // Reset Leap override on shift down.
    if (!shift_key && shift_key_) {
      leap_override_ = false;
    }

    // Perform Super Jump on space down while shift is held.
    if (shift_key_ && (!space_key && space_key_)) {
      client_.mask(rock::button::down, 16ms);
      leap_override_ = true;
      return;
    }

    // Schedule Leap on shift up or s down while shift is held and if not already leaping or overridden.
    if (!leap_ && !leap_override_ && ((shift_key && !shift_key_) || (shift_key_ && !s_key && s_key_))) {
      client_.mask(rock::button::up, 0ms);
      leap_timeout_ = frame + 16ms;
      leap_ = true;
      return;
    }

    // Leap on timeout.
    if (leap_) {
      if (frame > leap_timeout_) {
        client_.mask(rock::button::up, 32ms);
        jump_timeout_ = frame + 16ms;
        jump_ = true;
        leap_ = false;
      }
      return;
    }

    // Start Jump on space down.
    if (!jump_ && space_key_) {
      client_.mask(rock::button::up, 2s);
      jump_timeout_ = frame + 1s;
      jump_ = true;
      return;
    }

    // Stop Jump on space up.
    if (jump_ && !space_key_) {
      client_.mask(rock::button::up, 0ms);
      jump_timeout_ = {};
      jump_ = false;
      return;
    }

    // Update Jump on timeout.
    if (jump_ && frame > jump_timeout_) {
      client_.mask(rock::button::up, 2s);
      jump_timeout_ = frame + 1s;
    }
  }

  void enable() noexcept
  {
    enabled_ = true;
  }

  void disable() noexcept
  {
    if (enabled_) {
      client_.mask(rock::button::up, std::chrono::milliseconds(0));
      leap_timeout_ = {};
      jump_timeout_ = {};
      leap_ = false;
      jump_ = false;
    }
    enabled_ = false;
  }

private:
  eye& eye_;
  rock::client& client_;
  bool enabled_{ true };

  bool s_key_{ false };
  bool space_key_{ false };
  bool shift_key_{ false };

  bool leap_{ false };
  bool leap_override_{ false };
  clock::time_point leap_timeout_{};

  bool jump_{ false };
  clock::time_point jump_timeout_{};
};

}  // namespace horus::hero