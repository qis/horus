#pragma once
#include "config.hpp"
#include <dinput.h>
#include <dinputd.h>
#include <rock/config.hpp>
#include <cstdint>

namespace horus {

class HORUS_API hid {
public:
  struct mouse {
    std::uint8_t buttons;
    std::int32_t dx;
    std::int32_t dy;
  };

  hid() noexcept;

  hid(hid&& other) = delete;
  hid(const hid& other) = delete;
  hid& operator=(hid&& other) = delete;
  hid& operator=(const hid& other) = delete;

  ~hid();

  bool get(mouse& state) noexcept;

private:
  LPDIRECTINPUT8 input_{ nullptr };
  LPDIRECTINPUTDEVICE8 device_{ nullptr };
  DIMOUSESTATE2 state_{};
};

}  // namespace horus