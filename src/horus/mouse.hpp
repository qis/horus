#pragma once
#include "config.hpp"
#include <dinput.h>
#include <dinputd.h>

namespace horus {

class HORUS_API mouse {
public:
  struct state {
    long mx = 0;      // mouse movement since last get call (horizontal)
    long my = 0;      // mouse movement since last get call (vertical)
    bool bl = false;  // button state: left
    bool br = false;  // button state: right
    bool bm = false;  // button state: middle
    bool bd = false;  // button state: down
    bool bu = false;  // button state: up
  };

  mouse() noexcept;
  mouse(mouse&& other) = delete;
  mouse(const mouse& other) = delete;
  mouse& operator=(mouse&& other) = delete;
  mouse& operator=(const mouse& other) = delete;
  ~mouse();

  bool get(state& state) noexcept;

private:
  LPDIRECTINPUT8 input_{ nullptr };
  LPDIRECTINPUTDEVICE8 device_{ nullptr };
  DIMOUSESTATE2 state_{};
};

}  // namespace horus