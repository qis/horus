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
    const auto q_state = q_state_;
    q_state_ = keybd.q;

    const auto space_state = space_state_;
    space_state_ = keybd.space;

    const auto control_state = control_state_;
    control_state_ = keybd.control;

    const auto menu_state = menu_state_;
    menu_state_ = keybd.menu;

    const auto down_state = down_state_;
    down_state_ = mouse.down;

    // Skip when not enabled.
    if (!enabled_) {
      return;
    }

    // Stop flying on down button press and schedule jump.
    if ((!down_state && down_state_)) {
      client_.mask(rock::button::up, 0ms);
      jump_timeout_ = frame + 32ms;
      jump_ = true;
      fly_ = false;
      return;
    }

    // Perform jump on jump timeout.
    if (jump_) {
      if (frame > jump_timeout_) {
        client_.mask(rock::button::up, 7ms);
        jump_timeout_ = {};
        jump_ = false;
      }
      return;
    }

    // Enter Valkyrie mode on Q key press.
    if (!q_state && q_state_) {
      valkyrie_ = true;
      valkyrie_timeout_ = frame + 15s;
      client_.mask(rock::button::up, 0ms);
      fly_update_ = {};
      fly_ = false;
      return;
    }

    // Exit Valkyrie mode on menu key press or timeout.
    if (valkyrie_) {
      if ((!menu_state && menu_state_) || frame > valkyrie_timeout_) {
        valkyrie_ = false;
        valkyrie_timeout_ = {};
        client_.mask(rock::button::up, 2s);
        fly_update_ = frame + 1s;
        fly_ = true;
      } else if (!fly_ && space_state_) {
        client_.mask(rock::button::up, 2s);
        fly_update_ = frame + 1s;
        fly_ = true;
      } else if (fly_ && !space_state_) {
        client_.mask(rock::button::up, 0ms);
        fly_update_ = {};
        fly_ = false;
      } else if (fly_ && frame > fly_update_) {
        client_.mask(rock::button::up, 2s);
        fly_update_ = frame + 1s;
      }
      return;
    }

    // Suspend flying on space key press.
    if (!space_state && space_state_) {
      client_.mask(rock::button::up, 0ms);
      fly_suspend_timeout_ = frame + 32ms;
      fly_suspend_ = true;
      return;
    }

    // Resume or start flying on suspend timeout.
    if (fly_suspend_) {
      if (frame > fly_suspend_timeout_) {
        fly_suspend_ = false;
        fly_suspend_timeout_ = {};
        client_.mask(rock::button::up, 2s);
        fly_update_ = frame + 1s;
        fly_ = true;
      }
      return;
    }

    // Stop flying on control key press.
    if ((!control_state && control_state_)) {
      client_.mask(rock::button::up, 0ms);
      fly_update_ = {};
      fly_ = false;
      return;
    }

    // Start flying on control key release.
    if ((control_state && !control_state_)) {
      client_.mask(rock::button::up, 2s);
      fly_update_ = frame + 1s;
      fly_ = true;
      return;
    }

    // Keep flying.
    if (fly_ && frame > fly_update_) {
      client_.mask(rock::button::up, 2s);
      fly_update_ = frame + 1s;
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
      fly_update_ = {};
      enabled_ = false;
    }
  }

private:
  eye& eye_;
  rock::client& client_;
  bool enabled_{ true };

  bool q_state_{ false };
  bool space_state_{ false };
  bool control_state_{ false };
  bool menu_state_{ false };
  bool down_state_{ false };

  bool fly_{ false };
  clock::time_point fly_update_{};

  bool fly_suspend_{ false };
  clock::time_point fly_suspend_timeout_{};

  bool jump_{ false };
  clock::time_point jump_timeout_{};

  bool valkyrie_{ false };
  clock::time_point valkyrie_timeout_{};
};

}  // namespace horus::hero