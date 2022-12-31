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

    // Enable on menu and attack.
    if (!enabled_) {
      if (keybd.menu || mouse.left || mouse.right) {
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
#elif HERO_SOLDIER
    // How long it takes for a round to change.
    constexpr auto round_interval = 96ms;

    // How long to wait between recoil compensation injects.
    constexpr auto compensate_interval = 16ms;

    // How much to move the mouse for each round that has to be compensated.
    constexpr auto compensate_round = int16_t(59);

    // How often to move the mouse between rounds.
    constexpr auto compensate_count = (round_interval - compensate_interval) / compensate_interval;

    // Maximum mouse movement per update.
    constexpr auto compensate_max = compensate_round / compensate_count;

    // Update fire state, reload state and ammo count.
    const auto fire_state = fire_state_;
    fire_state_ = mouse.left;

    const auto reload_state = reload_state_;
    reload_state_ = keybd.r;

    const auto ammo_count = ammo_count_;
    if (const auto [tc, te, oc, oe] = eye_.ammo(data); oe < 0.1f) {
      auto ac = oc;
      if (te < 0.1f) {
        ac += tc * 10;
      } else if (te < 0.2f) {
        ac += ammo_count_ / 10 * 10;
      }
      if (ac == 30 || ac < ammo_count_ || ammo_count_ == 0) {
        ammo_count_ = ac;
      }
    }

    if ((!fire_state && fire_state_) || (!reload_state && reload_state_)) {
      compensate_next_ = frame + compensate_interval;
      ammo_compensated_ = 0;
      ammo_start_ = ammo_count_ ? ammo_count_ : 30;
    } else if (fire_state && !fire_state_) {
      compensate_ = 0;
    } else if (ammo_count < 30 && ammo_count_ == 30) {
      ammo_compensated_ = 0;
      ammo_start_ = 30;
    }

    // Set aim correction.
    int16_t correction = 0;
    if (fire_state_ && ammo_count_ > 0 && ammo_count_ < 30) {
      if (const auto pt = eye_.find()) {
        // Center of screen position.
        constexpr auto psx = static_cast<int>(eye::sw / 2);

        // Mouse position.
        const auto pmx = psx + static_cast<int>(mouse.dx);

        // Target position.
        const auto ptx = pt->x;

        // Distance between center and mouse.
        const auto smx = std::abs(psx - pmx);

        // Distance between center and target.
        const auto stx = std::abs(psx - ptx);

        // Distance between target and mouse.
        const auto tmx = std::abs(ptx - pmx);

        if (tmx > 3) {
          if (tmx < 9) {
            // Snap to target if the distance between target and mouse is small.
            correction = static_cast<int16_t>(ptx - pmx) * 2;
          } else if (tmx < 64) {
            if ((pmx - 3 < psx && ptx < pmx - 3) || (pmx + 3 > psx && ptx > pmx + 3)) {
              // Move to target if the target is further from the center, than the mouse.
              correction = static_cast<int16_t>(ptx - pmx);
            } else {
              // Adjust aim if the target is closer to the center, than the mouse.
              correction = static_cast<int16_t>(ptx - pmx) / 2;
            }
          }
        }
      }
    }

    // Perform scheduled recoil compensation.
    if (frame > compensate_next_) {
      if (compensate_ > 0) {
        if (compensate_ < compensate_max) {
          client_.move(1, correction, compensate_);
          compensate_ = 0;
        } else if (compensate_ > compensate_max * 3) {
          client_.move(1, correction, compensate_max * 2);
          compensate_ -= compensate_max * 2;
        } else {
          client_.move(1, correction, compensate_max);
          compensate_ -= compensate_max;
        }
      } else if (correction != 0) {
        client_.move(1, correction, 0);
      }
      compensate_next_ = frame + compensate_interval;
    }

    // Schedule recoil compensation.
    if (ammo_count_ < ammo_count && ammo_count_ < ammo_start_) {
      const auto bullets_fired = ammo_start_ - ammo_count_;
      for (auto i = ammo_compensated_; i < bullets_fired; i++) {
        if (i > 9) {
          compensate_ += compensate_round;
        } else if (i > 1) {
          compensate_ += compensate_round;
        }
        ammo_compensated_++;
      }
      compensate_next_ = frame + compensate_interval;
    }

    return;
#else
    // Skip when right mouse button is not pressed.
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
  clock::time_point blocked_{};
  bool enabled_{ true };

#if HERO_SUPPORT
  bool shift_state_{ false };
#elif HERO_SOLDIER
  bool fire_state_{ false };
  bool reload_state_{ false };

  unsigned ammo_count_{ 0 };
  unsigned ammo_start_{ 0 };
  unsigned ammo_compensated_{ 0 };

  int16_t compensate_{ 0 };
  clock::time_point compensate_next_{};
#endif
};

}  // namespace horus::hero