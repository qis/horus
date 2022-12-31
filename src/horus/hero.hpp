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

    // Target acquired.
    auto ta = false;

    // Adjust view.
    auto ax = 0;
    auto ay = 0;

    // Handle target.
    if (target) {
      // Center position.
      constexpr auto cw = static_cast<int>(eye::sw / 2);
      constexpr auto ch = static_cast<int>(eye::sh / 2);

      // Mouse position.
      const auto mx = cw + mouse.dx;
      const auto my = ch + mouse.dy;
      
      // Target to mouse offset.
      const auto dx = target->point.x - mx;
      const auto dy = target->point.y - my;

      // Increase and rotate history index.
      history_index_++;
      if (history_index_ >= history_size) {
        history_index_ = 0;
      }

      // Check if the current target is close enough to the predicted mouse position.
      if (!ta && target->distance < 64.0) {
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

    // Check if view adjustments have acceptable values.
    if (ta && (std::abs(ax) > 16 || std::abs(ax) > target->cw / 2)) {
      ta = false;
    }

    // Adjust view and inject mouse button press if target is acquired.
    if (ta && frame >= blocked_) {
      client_.lock(32ms);
      if (ax != 0 || ay != 0) {
        client_.move(1, static_cast<int16_t>(ax * 5), static_cast<int16_t>(ay * 3));
      }
      client_.mask(rock::button::up, 7ms);
    } else {
      ta = false;
    }

    // Block inject on left mouse button or after inject.
    if (mouse.left || ta) {
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

  static constexpr std::size_t history_size = 2;
  std::size_t history_entries_{ 0 };
  std::array<eye::target, history_size> history_{};
  std::size_t history_index_{ 0 };
};

}  // namespace horus::hero