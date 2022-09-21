#pragma once
#include "config.hpp"
#include <dinput.h>
#include <dinputd.h>
#include <cstdint>

namespace horus {

class HORUS_API hid {
public:
  struct mouse {
    bool left;
    bool right;
    bool middle;
    bool down;
    bool up;
    std::int32_t dx;
    std::int32_t dy;
  };

  struct keybd {
    bool q;
    bool e;
    bool shift;
    bool space;
    bool control;
    bool menu;
    bool win;
  };

  hid() noexcept;

  hid(hid&& other) = delete;
  hid(const hid& other) = delete;
  hid& operator=(hid&& other) = delete;
  hid& operator=(const hid& other) = delete;

  ~hid();

  bool get(keybd& state) noexcept;
  bool get(mouse& state) noexcept;

private:
  LPDIRECTINPUT8 input_{ nullptr };

  LPDIRECTINPUTDEVICE8 keybd_{ nullptr };
  std::uint8_t keybd_state_[256]{};

  LPDIRECTINPUTDEVICE8 mouse_{ nullptr };
  DIMOUSESTATE2 mouse_state_{};
  
};

}  // namespace horus