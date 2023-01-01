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

    const auto q_state = q_state_;
    q_state_ = keybd.q;

    const auto shift_state = shift_state_;
    shift_state_ = keybd.shift;

    const auto space_state = space_state_;
    space_state_ = keybd.space;

    const auto control_state = control_state_;
    control_state_ = keybd.control;

    const auto menu_state = menu_state_;
    menu_state_ = keybd.menu;

    // Skip when not enabled.
    if (!enabled_) {
      return;
    }

    // Enter Valkyrie mode on Q key press.
    if (!q_state && q_state_) {
      client_.mask(rock::button::up, 0ms);
      valkyrie_start_ = frame;
      valkyrie_ = true;
      return;
    }

    // Exit Valkyrie mode on menu key press or timeout.
    if (valkyrie_) {
      if ((!menu_state && menu_state_) || (frame > valkyrie_start_ + 15s)) {
        client_.mask(rock::button::up, 2s);
        update_ = frame + 1s;
        valkyrie_ = false;
        fly_ = true;
      }
      return;
    }

    // Enter Guardian Angel mode on shift key press.
    if (!shift_state && shift_state_) {
      //client_.mask(rock::button::up, 0ms);
      guardian_angel_ = true;
      //fly_ = false;
    }

    // Exit Guardian Angel mode on control or space key release.
    if (guardian_angel_) {
      if ((control_state && !control_state_) || (space_state && !space_state_)) {
        client_.mask(rock::button::up, 2s);
        guardian_angel_ = false;
        update_ = frame + 1s;
        fly_ = true;
      }
      //return;
    }

    // Handle fly mode.
    if (fly_) {
      if (!space_state && space_state_) {
        // Stop flying on space key press.
        client_.mask(rock::button::up, 0ms);
        update_ = frame + 1s;
      } else if (space_state && !space_state_) {
        // Resume flying on space key release.
        client_.mask(rock::button::up, 2s);
        update_ = frame + 1s;
      } else if (!control_state && control_state_) {
        // Exit fly mode on control key press.
        client_.mask(rock::button::up, 0ms);
        fly_ = false;
      } else if (frame > update_) {
        // Continue flying.
        client_.mask(rock::button::up, 2s);
        update_ = frame + 1s;
      }
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
      enabled_ = false;
    }
  }

private:
  eye& eye_;
  rock::client& client_;
  bool enabled_{ true };

  bool q_state_{ false };
  bool shift_state_{ false };
  bool space_state_{ false };
  bool control_state_{ false };
  bool menu_state_{ false };

  bool guardian_angel_{ false };

  bool valkyrie_{ false };
  clock::time_point valkyrie_start_{};

  bool fly_{ false };
  clock::time_point update_{};
};

}  // namespace horus::hero