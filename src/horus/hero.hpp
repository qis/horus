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

    const auto q_key = q_key_;
    q_key_ = keybd.q;

    const auto space_key = space_key_;
    space_key_ = keybd.space;

    const auto shift_key = shift_key_;
    shift_key_ = keybd.shift;

    const auto menu_key = menu_key_;
    menu_key_ = keybd.menu;

    // Skip when not enabled.
    if (!enabled_) {
      return;
    }

    // Required Bindings
    // ==============================
    // CROUCH | MOUSE 4 | LCONTROL
    // JUMP   | MOUSE 5 |

    // Enter Valkyrie mode on q down.
    if (!q_key && q_key_) {
      valkyrie_timeout_ = frame + 15s;
      valkyrie_ = true;
      if (!space_key_) {
        client_.mask(rock::button::up, 0ms);
        glide_ = false;
      }
    }

    // Exit Valkyrie mode on menu down or timeout.
    if (valkyrie_ && ((!menu_key && menu_key_) || frame > valkyrie_timeout_)) {
      valkyrie_ = false;
      glide_ = true;
      glide_update_ = {};
    }

    // Reset Glide override on shift down.
    if (!shift_key && shift_key_) {
      glide_override_ = false;
    }

    // Perform Super Jump on space down while shift is held.
    if (shift_key_ && (!space_key && space_key_)) {
      client_.mask(rock::button::up, 0ms);
      client_.mask(rock::button::down, 16ms);
      glide_update_ = frame + 32ms;
      glide_override_ = true;
      glide_ = true;
      return;
    }

    // Start Glide on shift up or s down while shift is held if not overridden.
    if (!glide_override_ && ((shift_key && !shift_key_) || (shift_key_ && !s_key && s_key_))) {
      client_.mask(rock::button::up, 0ms);
      glide_update_ = frame + 32ms;
      glide_ = true;
      return;
    }

    // Start Glide on space down.
    if (!shift_key_ && !space_key && space_key_) {
      client_.mask(rock::button::up, 0ms);
      glide_update_ = frame + 32ms;
      glide_ = true;
      return;
    }

    // Stop Glide on space up.
    if (!shift_key_ && space_key && !space_key_) {
      if (glide_override_) {
        glide_override_ = false;
      } else {
        client_.mask(rock::button::up, 0ms);
        glide_ = false;
      }
      return;
    }

    // Handle Glide.
    if (glide_ && frame > glide_update_) {
      client_.mask(rock::button::up, valkyrie_ && !space_key_ ? 8ms : 2s);
      glide_update_ = frame + 1s;
      if (valkyrie_ && !space_key_) {
        glide_ = false;
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
      glide_update_ = {};
      glide_ = false;
    }
    enabled_ = false;
  }

private:
  eye& eye_;
  rock::client& client_;
  bool enabled_{ true };

  bool s_key_{ false };
  bool q_key_{ false };
  bool space_key_{ false };
  bool shift_key_{ false };
  bool menu_key_{ false };

  bool glide_{ false };
  bool glide_override_{ false };
  clock::time_point glide_update_{};

  bool valkyrie_{ false };
  clock::time_point valkyrie_timeout_{};
};

}  // namespace horus::hero