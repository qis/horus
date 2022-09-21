#pragma once
#include <horus/hid.hpp>

namespace horus::hero {

enum class type : unsigned {
  ana,
  ashe,
  brigitte,
  pharah,
  reaper,
  none,
};

inline constexpr const char* name(type value) noexcept
{
  // clang-format off
  switch (value) {
  case type::ana: return "ana";
  case type::ashe: return "ashe";
  case type::brigitte: return "brigitte";
  case type::pharah: return "pharah";
  case type::reaper: return "reaper";
  case type::none: return "none";
  }
  return "unknown";
  // clang-format on
}

class base {
public:
  virtual ~base() = default;

  /// Searches image for world or user interface elements.
  ///
  /// @param image Unmodified image from Overwatch (sw x sh 4 byte rgba).
  /// @param mouse Current mouse state.
  ///
  /// @return Returns true if the eye::draw* functions can be used after this call.
  ///
  virtual bool scan(std::uint8_t* data, const hid::mouse& mouse) noexcept = 0;
};

}  // namespace horus::hero