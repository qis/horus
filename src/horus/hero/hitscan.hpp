#pragma once
#include "base.hpp"
#include <horus/eye.hpp>
#include <rock/client.hpp>

namespace horus::hero {

class hitscan : public base {
public:
  using clock = std::chrono::high_resolution_clock;

  hitscan(eye& eye, rock::client& client) noexcept : eye_(eye), client_(client) {}

  status scan(
    std::uint8_t* data,
    const hid::keybd& keybd,
    const hid::mouse& mouse,
    clock::time_point frame) noexcept override
  {
    const auto ax = std::pow(std::abs(mouse.dx) * 1.5f, 1.05f);
    const auto ay = std::pow(std::abs(mouse.dy) * 1.5f, 1.05f);
    const auto mx = mouse.dx < 0 ? -ax : ax;
    const auto my = mouse.dy < 0 ? -ay : ay;

    const auto hs = eye_.scan(data, mx, my);

    auto set = false;

    if (frame >= hitscan_blocked_ && hs && !mouse.left && mouse.right != mouse.down) {
      client_.mask(rock::button::left, std::chrono::milliseconds(7));
      set = true;
    }

    if (mouse.left || set) {
      hitscan_blocked_ = frame + std::chrono::milliseconds(128);
    }

    return set ? static_cast<status>(status::draw | status::beep) : status::draw;
  }

protected:
  eye& eye_;
  rock::client& client_;

private:
  clock::time_point hitscan_blocked_{ clock::now() };
};

}  // namespace horus::hero