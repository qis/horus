#pragma once
#include "base.hpp"
#include <rock/client.hpp>

namespace horus::hero {

class brigitte : public base {
public:
  brigitte(rock::client& client) noexcept : client_(client) {}

  hero::type type() const noexcept override
  {
    return hero::type::brigitte;
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