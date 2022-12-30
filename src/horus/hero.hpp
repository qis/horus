#pragma once
#include <horus/eye.hpp>
#include <horus/hid.hpp>
#include <rock/client.hpp>

namespace horus::hero {

class hitscan {
public:
  using clock = std::chrono::high_resolution_clock;

  hitscan(eye& eye, rock::client& client) noexcept : eye_(eye), client_(client) {}

  void scan(
    std::uint8_t* data,
    const hid::keybd& keybd,
    const hid::mouse& mouse,
    clock::time_point frame) noexcept
  {
    using namespace std::chrono_literals;

#if HERO_JUNKRAT
    const auto down_state = down_state_;
    down_state_ = mouse.down;

    const auto right_state = right_state_;
    right_state_ = mouse.right;

    // Detonate mine with the secondary jump button.
    if (!down_state && down_state_) {
      client_.mask(rock::button::up, 7ms);
      return;
    }

    // Detonate mine while primary fire is pressed.
    if ((mouse.down || mouse.left) && frame >= mine_blocked_) {
      client_.mask(rock::button::up, 128ms);
      mine_blocked_ = frame + 64ms;
      return;
    }

    // Release secondary fire when mine is thrown and give some time to release primary fire.
    if (!right_state && right_state_) {
      client_.mask(rock::button::up, 0ms);
      mine_blocked_ = frame + 256ms;
      return;
    }

    return;
#endif

#if HERO_TRACER
    const auto down_state = down_state_;
    down_state_ = mouse.down;

    if (!down_state && down_state_) {
      client_.mask(rock::button::right, 7ms);
      move_blocked_ = frame + 32ms;
      move_ = true;
    } else if (move_ && frame > move_blocked_) {
      client_.move(1, 4'450, 0);
      move_ = false;
    }

    const auto shift_state = shift_state_;
    shift_state_ = keybd.shift;

    if (!shift_state && shift_state_) {
      client_.mask(rock::button::up, 7ms);
    }

    return;
#endif

    // Check if target is acquired.
    const auto target = eye_.scan(data, mouse.dx, mouse.dy);

    // Disable on enter.
    if (keybd.enter) {
      enabled_ = false;
      return;
    }

    // Enable on menu and attack.
    if (!enabled_) {
      if (keybd.menu || mouse.left || mouse.right) {
        enabled_ = true;
        if (mouse.left) {
          hitscan_blocked_ = frame + 128ms;
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
#if HERO_WIDOWMAKER
    const auto right_state = right_state_;
    right_state_ = mouse.right;
    if (!right_state && right_state_) {
      hitscan_blocked_ = frame + 1350ms;
    }
#endif
    // Skip when right mouse button is not pressed.
    if (!mouse.right) {
      return;
    }
#endif

    auto set = false;
    if (target && frame >= hitscan_blocked_) {
      client_.lock(32ms);
      client_.mask(rock::button::up, 7ms);
      set = true;
    }

    // Block execution to prevent left mouse button spam.
    if (mouse.left || set) {
#if HERO_WIDOWMAKER
      hitscan_blocked_ = frame + 1350ms;
#else
      hitscan_blocked_ = frame + 128ms;
#endif
    }

    return;

    // Aimbot template.
    //
    // TODO:
    // - Mouse acceleration has to be moved to the mouse.
    // - Properly convert target position to camera rotation.
    //
    // if (const auto pt = eye_.find()) {
    //   constexpr auto cw = static_cast<int>(eye::sw / 2);
    //   constexpr auto ch = static_cast<int>(eye::sh / 2);
    //   const auto cx = static_cast<std::int16_t>(pt->x - cw);
    //   const auto cy = static_cast<std::int16_t>(pt->y - ch);
    //   if (std::abs(cx) < 16 && std::abs(cy) < 16) {
    //     client_.mask(rock::button::up, 7ms);
    //     client_.lock(0ms);
    //     set = true;
    //   } else {
    //     client_.move(1, cx, cy);
    //   }
    // }
  }

private:
  eye& eye_;
  rock::client& client_;
  clock::time_point hitscan_blocked_{ clock::now() };
  bool enabled_{ true };
  bool ready_{ false };

#if HERO_SUPPORT
  bool shift_state_{ false };
#endif

#if HERO_JUNKRAT
  bool down_state_{ false };
  bool right_state_{ false };
  clock::time_point mine_blocked_{ clock::now() };
#endif

#if HERO_TRACER
  bool down_state_{ false };
  bool shift_state_{ false };

  bool move_{ false };
  clock::time_point move_blocked_{ clock::now() };
#endif

#if HERO_WIDOWMAKER
  bool right_state_{ false };
  unsigned target_frame_ = 0;
#endif
};

}  // namespace horus::hero