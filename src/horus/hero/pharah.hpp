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

  status scan(std::uint8_t* data, const hid::mouse& mouse, clock::time_point frame) noexcept override
  {
    // TODO: Process mouse state.
    return status::none;
  }

private:
  rock::client& client_;
};

}  // namespace horus::hero