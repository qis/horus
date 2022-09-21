#pragma once
#include "base.hpp"
#include <rock/client.hpp>
#include <chrono>

namespace horus::hero {

class pharah : public base {
public:
  static constexpr std::chrono::milliseconds config_boost_duration{ 1400 };
  static constexpr std::chrono::milliseconds config_flight_duration{ 350 };
  static constexpr std::chrono::milliseconds config_fall_duration{ 450 };
  static constexpr std::chrono::milliseconds config_skip_delta{ 50 };
  static_assert(config_skip_delta < config_flight_duration);

  pharah(rock::client& client) noexcept : client_(client) {}

  hero::type type() const noexcept override
  {
    return hero::type::pharah;
  }

  status scan(
    std::uint8_t* data,
    const hid::keybd& keybd,
    const hid::mouse& mouse,
    clock::time_point frame) noexcept override
  {
    if (keybd.space || mouse.right) {
      if (frame + std::chrono::milliseconds(500) > release_) {
        client_.mask(rock::button::middle, std::chrono::seconds(1));
        release_ = frame + std::chrono::seconds(1);
      }
    } else {
      if (frame < release_) {
        client_.mask(rock::button::middle, std::chrono::seconds(0));
        release_ = frame;
      }
    }
    return status::none;
  }

private:
  rock::client& client_;
  clock::time_point release_{ clock::now() };
};

}  // namespace horus::hero