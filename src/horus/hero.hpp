#pragma once
#include <horus/eye.hpp>
#include <horus/hid.hpp>
#include <horus/sound.hpp>
#include <rock/client.hpp>

namespace horus::hero {

class base {
public:
  using clock = std::chrono::high_resolution_clock;

  base(eye& eye, rock::client& client) noexcept : eye_(eye), client_(client) {}

  base(base&& other) = default;
  base(const base& other) = delete;

  base& operator=(base&& other) = default;
  base& operator=(const base& other) = delete;

  virtual ~base() = default;

  virtual std::string_view name() const noexcept = 0;

  virtual void scan(
    std::uint8_t* image,
    const hid::keybd& keybd,
    const hid::mouse& mouse,
    clock::time_point frame) noexcept = 0;

  void announce() noexcept
  {
    name_ = { std::format("C:/OBS/horus/src/rock/res/{}.wav", name()).data() };
    name_.play();
  }

protected:
  eye& eye_;
  rock::client& client_;
  sound name_;
};

class brigitte : public base {
public:
  brigitte(eye& eye, rock::client& client) noexcept : base(eye, client) {}

  std::string_view name() const noexcept override
  {
    return "brigitte";
  }

  void scan(
    std::uint8_t* image,
    const hid::keybd& keybd,
    const hid::mouse& mouse,
    clock::time_point frame) noexcept override
  {
    using namespace std::chrono_literals;

    // Required Movement Options
    // =========================
    // JUMP        | SPACE | MOUSE 5

    switch (trick_) {
    case state::input:
      if (frame > trick_update_ && mouse.down) {
        client_.mask(rock::button::up, 40ms);
        trick_update_ = frame + 40ms;
        trick_ = state::shield;
      }
      break;
    case state::shield:
      if (frame > trick_update_) {
        client_.mask(rock::button::right, 100ms);
        trick_update_ = frame + 40ms;
        trick_ = state::bash;
      }
      break;
    case state::bash:
      if (frame > trick_update_) {
        client_.mask(rock::button::left, 20ms);
        trick_update_ = frame + 60ms;
        trick_ = state::input;
      }
      break;
    }
  }

private:
  enum class state {
    input,
    shield,
    bash,
  };

  state trick_{ state::input };
  clock::time_point trick_update_{};
};

class mercy : public base {
public:
  using clock = std::chrono::high_resolution_clock;

  mercy(eye& eye, rock::client& client) noexcept : base(eye, client) {}

  std::string_view name() const noexcept override
  {
    return "mercy";
  }

  void scan(
    std::uint8_t* image,
    const hid::keybd& keybd,
    const hid::mouse& mouse,
    clock::time_point frame) noexcept override
  {
    using namespace std::chrono_literals;

    // Update input tricks.
    const auto q_key = q_key_;
    q_key_ = keybd.q;

    const auto s_key = s_key_;
    s_key_ = keybd.s;

    const auto space_key = space_key_;
    space_key_ = keybd.space;

    const auto shift_key = shift_key_;
    shift_key_ = keybd.shift;

    const auto menu_key = menu_key_;
    menu_key_ = keybd.menu;

    // Disable enter, escape, windows key and menu + tab.
    if (keybd.enter || keybd.escape || keybd.win || (keybd.menu && keybd.tab)) {
      if (enabled_) {
        client_.mask(rock::button::up, std::chrono::milliseconds(0));
        glide_update_ = {};
        glide_ = false;
      }
      enabled_ = false;
    }

    // Enable on menu.
    if (keybd.menu) {
      enabled_ = true;
    }

    // Skip when not enabled.
    if (!enabled_) {
      return;
    }

    // Stop Glide on b.
    if (glide_ && keybd.b) {
      client_.mask(rock::button::up, 0ms);
      glide_ = false;
    }

    // Required Movement Options
    // ===========================
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

    // Reset Glide override if space is released before the shift key.
    if (shift_key_ && space_key && !space_key_) {
      glide_override_ = false;
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

private:
  bool enabled_{ true };

  bool q_key_{ false };
  bool s_key_{ false };
  bool space_key_{ false };
  bool shift_key_{ false };
  bool menu_key_{ false };

  bool glide_{ false };
  bool glide_override_{ false };
  clock::time_point glide_update_{};

  bool valkyrie_{ false };
  clock::time_point valkyrie_timeout_{};
};

class reaper : public base {
public:
  reaper(eye& eye, rock::client& client) noexcept : base(eye, client) {}

  std::string_view name() const noexcept override
  {
    return "reaper";
  }

  void scan(
    std::uint8_t* image,
    const hid::keybd& keybd,
    const hid::mouse& mouse,
    clock::time_point frame) noexcept override
  {
    using namespace std::chrono_literals;

    // Skip when manually firing or blocked.
    if (mouse.left || frame < blocked_) {
      return;
    }

    // Required Reticle Options
    // ========================
    // TYPOE        | CROSSHAIRS
    // COLOR        | MAGENTA
    // DOT OPACITY  | 0%

    // Required Weapons & Abilities Options
    // ====================================
    // PRIMARY FIRE | MOUSE LEFT | MOUSE 5

    // The bullet spread is 80 px around the center of the screen.

    // Reticle position.
    constexpr auto rx = static_cast<int>(eye::sw / 2);
    constexpr auto ry = static_cast<int>(eye::sh / 2);

    // Predicted reticle position based on current mouse movement.
    const auto mx = rx + mouse.dx;
    const auto my = ry + mouse.dy;

#if 0
    // Get target closest to the predicted reticle position.
    if (const auto target = eye_.scan(image, mx, my)) {
      // Get center of mass.
      auto point = target->point;


      // Target to mouse offset.
      //const auto dx = target->point.x - mx;
      //const auto dy = target->point.y - my;

      // Increase and rotate history index.
      history_index_++;
      if (history_index_ >= history_size) {
        history_index_ = 0;
      }

      // Check if the current target is close enough to the predicted reticle position.
      if (target->distance < 64.0) {
        // Target acquired.
        ta = true;

        // Adjust view, so that the predicted mouse position points to the target.
        ax = dx;
        ay = dy;

        // Set ta, ax and ay based on target history.
        if (history_entries_ >= history_size) {
          // Create target history sorted by frame.
          // @ref history[0] is the oldest target
          // @ref history[history_size - 1] is the previous target
          // @ref history[history_size] is the current target
          std::array<const eye::target*, history_size + 1> history;
          for (size_t i = 0; i < history_size; i++) {
            history[i] = &history_[(history_index_ + i) % history_size];
          }
          history[history_size] = &*target;

          // Check if every target in history is close to the interpolation of the last two targets.
          auto same_target = true;
          for (std::size_t i = 2; i < history.size(); i++) {
            // Previous two targets.
            const auto t0 = history[i - 2];
            const auto t1 = history[i - 1];

            // Current target.
            const auto t2 = history[i];

            // Expected target position.
            const auto ex = t1->point.x + (t1->point.x - t0->point.x);
            const auto ey = t1->point.y + (t1->point.y - t0->point.y);

            // Distance between current target position and expected target position.
            const auto distance = cv::norm(t2->point - cv::Point(ex, ey));

            // Check if the distance is too large to indicate that this is the same target.
            if (distance > 128.0) {
              same_target = false;
              break;
            }
          }

          // Use history to predict target movement.
          if (same_target) {
            // Previous target.
            const auto t0 = history[history.size() - 2];

            // Current target.
            const auto t1 = history[history.size() - 1];

            // Expected target position.
            const auto ex = t1->point.x + (t1->point.x - t0->point.x);
            const auto ey = t1->point.y + (t1->point.y - t0->point.y);

            // Check if the expected target position is close enough to the predicted mouse position.
            if (cv::norm(cv::Point(mx, my) - cv::Point(ex, ey)) < 64.0) {
              // Compensate for target movement.
              ax += ex - t1->point.x;
              ay += ey - t1->point.y;
            }
          }
        } else {
          // Reset consecutive points counter.
          history_entries_ = 0;
        }
      }

      // Replace the oldest history entry.
      history_[history_index_] = *target;

      // Increase and limit consecutive points counter.
      history_entries_++;
      if (history_entries_ > history_size) {
        history_entries_ = history_size;
      }
    } else {
      // Reset consecutive points counter.
      history_entries_ = 0;
    }
#endif
  }

private:
  clock::time_point blocked_{};

#if 0
  static constexpr std::size_t history_size = 2;
  std::size_t history_entries_{ 0 };
  std::array<eye::target, history_size> history_{};
  std::size_t history_index_{ 0 };
#endif
};

class soldier : public base {
public:
  soldier(eye& eye, rock::client& client) noexcept : base(eye, client) {}

  std::string_view name() const noexcept override
  {
    return "soldier";
  }

  void scan(
    std::uint8_t* image,
    const hid::keybd& keybd,
    const hid::mouse& mouse,
    clock::time_point frame) noexcept override
  {
    using namespace std::chrono_literals;

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
      compensate_next_ = frame + ammo_interval * 5 / 2;
      compensate_ = true;
    } else if (compensate_ && frame > compensate_next_) {
      client_.move(1, 0, compensate_value);
      compensate_next_ = frame + compensate_interval;
      compensate_ = frame < compensate_ammo_;
    }
  }

private:
  bool fire_state_{ false };
  bool compensate_{ false };
  clock::time_point compensate_next_{};
  clock::time_point compensate_ammo_{};
};

std::unique_ptr<base> next(const std::unique_ptr<base>& hero, eye& eye, rock::client& client)
{
  std::unique_ptr<base> next;
  if (!hero || hero->name() == "soldier") {
    next = std::make_unique<brigitte>(eye, client);
  } else if (hero->name() == "brigitte") {
    next = std::make_unique<mercy>(eye, client);
  } else if (hero->name() == "mercy") {
    next = std::make_unique<reaper>(eye, client);
  } else if (hero->name() == "reaper") {
    next = std::make_unique<soldier>(eye, client);
  }
  if (next) {
    next->announce();
  }
  return next;
}

}  // namespace horus::hero