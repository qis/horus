#pragma once
#include "hitscan.hpp"

namespace horus::hero {

class ana : public hitscan {
public:
  ana(eye& eye, rock::client& client) noexcept : hitscan(eye, client) {}

  hero::type type() const noexcept override
  {
    return hero::type::ana;
  }
};

}  // namespace horus::hero