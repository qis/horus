#pragma once
#include <horus/hid.hpp>

namespace horus::hero {

enum class type : std::uint8_t {
  ana,
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
  case type::brigitte: return "brigitte";
  case type::pharah: return "pharah";
  case type::reaper: return "reaper";
  case type::none: return "none";
  }
  return "unknown";
  // clang-format on
}

enum status : std::uint8_t {
  none = 0x00,
  draw = 0x01,
  beep = 0x02,
};

using clock = std::chrono::high_resolution_clock;

class base {
public:
  virtual ~base() = default;

  virtual type type() const noexcept = 0;

  /// Searches image for world or user interface elements.
  ///
  /// @param image Unmodified image from Overwatch (sw x sh 4 byte rgba).
  /// @param mouse Current mouse state.
  ///
  /// @return Returns true if the eye::draw* functions can be used after this call.
  ///
  virtual status scan(std::uint8_t* data, const hid::mouse& mouse, clock::time_point frame) noexcept = 0;
};

}  // namespace horus::hero