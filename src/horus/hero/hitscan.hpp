#pragma once
#include "base.hpp"
#include <horus/eye.hpp>
#include <rock/client.hpp>

namespace horus::hero {

class hitscan : public base {
public:
  using clock = std::chrono::high_resolution_clock;

  hitscan(eye& eye, rock::client& client) noexcept : eye_(eye), client_(client) {}

  bool scan(std::uint8_t* data, const hid::mouse& mouse) noexcept override
  {
    const auto bl = mouse.buttons & rock::button::left;
    const auto br = mouse.buttons & rock::button::right;
    const auto bu = mouse.buttons & rock::button::up;

    const auto ax = std::pow(std::abs(mouse.dx) * 1.5f, 1.05f);
    const auto ay = std::pow(std::abs(mouse.dy) * 1.5f, 1.05f);

    const auto mx = mouse.dx < 0 ? -ax : ax;
    const auto my = mouse.dy < 0 ? -ay : ay;

    const auto hs = eye_.scan(data, mx, my);

    auto in = false;
    if (hs && ready_ && !bl && br != bu) {
      client_.mask(rock::button::left, std::chrono::milliseconds(7));
      in = true;
    }

    if (bl || in) {
      blocked_ = clock::now() + std::chrono::milliseconds(125);
      ready_ = false;
    } else {
      ready_ = clock::now() >= blocked_;
    }
    return true;
  }

protected:
  eye& eye_;
  rock::client& client_;
  clock::time_point blocked_{ clock::now() };
  bool ready_{ false };
};

}  // namespace horus::hero