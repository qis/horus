#pragma once
#include "hitscan.hpp"

namespace horus::hero {

class reaper : public hitscan {
public:
  reaper(eye& eye, rock::client& client) noexcept : hitscan(eye, client) {}

  hero::type type() const noexcept override
  {
    return hero::type::reaper;
  }
};

}  // namespace horus::hero