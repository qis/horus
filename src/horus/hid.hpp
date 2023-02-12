#pragma once
#include "config.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <dinput.h>
#include <dinputd.h>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdint>

namespace horus {

enum class button : std::uint8_t {
  left = 0,
  right,
  middle,
  down,
  up,
  none,
};

enum class key : std::uint8_t {
  // clang-format off
  escape  = DIK_ESCAPE,
  tab     = DIK_TAB,
  q       = DIK_Q,
  w       = DIK_W,
  e       = DIK_E,
  r       = DIK_R,
  enter   = DIK_RETURN,
  control = DIK_LCONTROL,
  s       = DIK_S,
  shift   = DIK_LSHIFT,
  c       = DIK_C,
  b       = DIK_B,
  alt     = DIK_LMENU,
  space   = DIK_SPACE,
  f6      = DIK_F6,
  f7      = DIK_F7,
  f8      = DIK_F8,
  f9      = DIK_F9,
  f10     = DIK_F10,
  f11     = DIK_F11,
  f12     = DIK_F12,
  pause   = DIK_PAUSE,
  win     = DIK_LWIN,
  // clang-format on
};

class HORUS_API hid {
public:
  static constexpr std::chrono::seconds maximum_mask_duration{ 10 };

  struct mouse {
    std::int32_t mx{ 0 };
    std::int32_t my{ 0 };
    clock::time_point tp{};
  };

  hid(boost::asio::any_io_executor executor) noexcept;

  hid(hid&& other) = delete;
  hid(const hid& other) = delete;
  hid& operator=(hid&& other) = delete;
  hid& operator=(const hid& other) = delete;

  ~hid();

  bool update() noexcept;

  hid::mouse movement() noexcept
  {
    const auto mm = mouse_movement_shared_.exchange(0);
    const auto mx = static_cast<std::int32_t>(static_cast<std::uint32_t>(mm >> 32));
    const auto my = static_cast<std::int32_t>(static_cast<std::uint32_t>(mm & 0xFFFFFFFF));
    return { mx, my, std::exchange(tp_, clock::now()) };
  }

  constexpr bool up(button button) const noexcept
  {
    return !current(button);
  }

  constexpr bool down(button button) const noexcept
  {
    return current(button);
  }

  constexpr bool pressed(button button) const noexcept
  {
    return !previous(button) && current(button);
  }

  constexpr bool released(button button) const noexcept
  {
    return previous(button) && !current(button);
  }

  constexpr bool up(key key) const noexcept
  {
    return !current(key);
  }

  constexpr bool down(key key) const noexcept
  {
    return current(key);
  }

  constexpr bool pressed(key key) const noexcept
  {
    return !previous(key) && current(key);
  }

  constexpr bool released(key key) const noexcept
  {
    return previous(key) && !current(key);
  }

  void mask(button button, std::chrono::milliseconds duration) noexcept;
  void move(std::int16_t x, std::int16_t y) noexcept;

private:
  constexpr bool current(button button) const noexcept
  {
    return mouse_state_[0].rgbButtons[static_cast<std::uint8_t>(button)] != 0x00;
  }

  constexpr bool previous(button button) const noexcept
  {
    return mouse_state_[1].rgbButtons[static_cast<std::uint8_t>(button)] != 0x00;
  }

  constexpr bool current(key key) const noexcept
  {
    return keybd_state_[0][static_cast<std::uint8_t>(key)] & 0x80 ? true : false;
  }

  constexpr bool previous(key key) const noexcept
  {
    return keybd_state_[1][static_cast<std::uint8_t>(key)] & 0x80 ? true : false;
  }

  LPDIRECTINPUT8 input_{ nullptr };

  LPDIRECTINPUTDEVICE8 keybd_{ nullptr };
  std::array<std::vector<std::uint8_t>, 3> keybd_state_;

  LPDIRECTINPUTDEVICE8 mouse_{ nullptr };
  std::array<DIMOUSESTATE2, 3> mouse_state_;

  std::int32_t mx_{ 0 };
  std::int32_t my_{ 0 };
  clock::time_point tp_{ clock::now() };
  std::uint64_t mouse_movement_{ 0 };
  std::atomic_uint64_t mouse_movement_shared_{ 0 };

  boost::asio::ip::udp::socket socket_;
  boost::asio::ip::udp::endpoint endpoint_;
  std::array<std::uint8_t, 4> data_{};
};

}  // namespace horus