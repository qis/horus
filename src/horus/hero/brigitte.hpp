#pragma once
#include "base.hpp"
#include <rock/client.hpp>

namespace horus::hero {

class brigitte : public base {
public:
  brigitte(rock::client client) noexcept : client_(client) {}

  bool scan(std::uint8_t* data, const hid::mouse& mouse) noexcept override
  {
    // TODO: Process mouse state.
    return false;
  }

private:
  rock::client& client_;
};

}  // namespace horus::hero