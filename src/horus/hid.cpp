#include "hid.hpp"
#include <algorithm>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

namespace horus {
namespace {

constexpr std::size_t keybd_state_size = 256;
constexpr std::size_t mouse_state_size = sizeof(DIMOUSESTATE2);

}  // namespace

hid::hid(boost::asio::any_io_executor executor) noexcept : timer_(executor), socket_(executor)
{
  boost::system::error_code ec;
  socket_.open(boost::asio::ip::udp::v4(), ec);
  if (!ec) {
    const auto endpoints = boost::asio::ip::udp::resolver(executor).resolve(
      boost::asio::ip::udp::v4(),
      HORUS_HID_ADDRESS,
      HORUS_HID_SERVICE,
      ec);
    if (!ec && !endpoints.empty()) {
      endpoint_ = *endpoints.begin();
    }
  }

  struct enum_windows_data {
    DWORD pid = GetCurrentProcessId();
    HWND hwnd = GetConsoleWindow();
  } ewd;

  if (!ewd.hwnd) {
    EnumWindows(
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        const auto ewd = reinterpret_cast<enum_windows_data*>(lparam);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == ewd->pid) {
          ewd->hwnd = hwnd;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ewd));
  }
  if (!ewd.hwnd) {
    return;
  }

  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
    return;
  }

  const auto hr = DirectInput8Create(
    GetModuleHandle(nullptr),
    DIRECTINPUT_VERSION,
    IID_IDirectInput8,
    reinterpret_cast<LPVOID*>(&input_),
    nullptr);

  if (FAILED(hr)) {
    input_ = nullptr;
    return;
  }

  LPDIRECTINPUTDEVICE8 keybd{ nullptr };
  if (SUCCEEDED(input_->CreateDevice(GUID_SysKeyboard, &keybd, nullptr))) {
    if (SUCCEEDED(keybd->SetDataFormat(&c_dfDIKeyboard))) {
      if (SUCCEEDED(keybd->SetCooperativeLevel(ewd.hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE))) {
        if (SUCCEEDED(keybd->Acquire())) {
          keybd_ = keybd;
        }
      }
    }
  }
  for (auto& keybd_state : keybd_state_) {
    keybd_state.resize(keybd_state_size, std::uint8_t(0));
  }

  LPDIRECTINPUTDEVICE8 mouse{ nullptr };
  if (SUCCEEDED(input_->CreateDevice(GUID_SysMouse, &mouse, nullptr))) {
    if (SUCCEEDED(mouse->SetDataFormat(&c_dfDIMouse2))) {
      if (SUCCEEDED(mouse->SetCooperativeLevel(ewd.hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE))) {
        if (SUCCEEDED(mouse->Acquire())) {
          mouse_ = mouse;
        }
      }
    }
  }
  std::memset(mouse_state_.data(), 0, mouse_state_.size() * mouse_state_size);
}

hid::~hid()
{
  if (mouse_) {
    mouse_->Unacquire();
    mouse_->Release();
  }
  if (keybd_) {
    keybd_->Unacquire();
    keybd_->Release();
  }
  if (input_) {
    input_->Release();
  }
}

bool hid::update() noexcept
{
  if (FAILED(keybd_->GetDeviceState(keybd_state_size, keybd_state_[2].data()))) {
    keybd_->Acquire();
    return false;
  }
  if (FAILED(mouse_->GetDeviceState(mouse_state_size, &mouse_state_[2]))) {
    mouse_->Acquire();
    return false;
  }

  std::swap(keybd_state_[1], keybd_state_[0]);
  std::swap(keybd_state_[0], keybd_state_[2]);
  mouse_state_[1] = mouse_state_[0];
  mouse_state_[0] = mouse_state_[2];

  const auto mx = static_cast<std::int32_t>(mouse_state_[0].lX);
  const auto my = static_cast<std::int32_t>(mouse_state_[0].lY);

  mx_ += mx;
  my_ += my;

  const auto ax = static_cast<std::uint64_t>(static_cast<std::uint32_t>(mx_));
  const auto ay = static_cast<std::uint64_t>(static_cast<std::uint32_t>(my_));
  auto mm = (ax << 32) + (ay & 0xFFFFFFFF);

  if (mouse_movement_shared_.compare_exchange_weak(mouse_movement_, mm)) {
    mouse_movement_ = mm;
    return true;
  }

  mx_ = mx;
  my_ = my;

  const auto ux = static_cast<std::uint64_t>(static_cast<std::uint32_t>(mx_));
  const auto uy = static_cast<std::uint64_t>(static_cast<std::uint32_t>(my_));
  mouse_movement_ = (ux << 32) + (uy & 0xFFFFFFFF);
  mouse_movement_shared_.store(mouse_movement_);

  return true;
}

void hid::mask(
  button button,
  std::chrono::milliseconds duration,
  std::chrono::steady_clock::duration delay) noexcept
{
  if (delay > std::chrono::steady_clock::duration(0)) {
    timer_.expires_from_now(delay);
    timer_.async_wait([this, button, duration](boost::system::error_code ec) noexcept {
      mask(button, duration);
    });
    return;
  }

  switch (button) {
  case button::left:
    data_[0] = 0x01 << 0;
    break;
  case button::right:
    data_[0] = 0x01 << 1;
    break;
  case button::middle:
    data_[0] = 0x01 << 2;
    break;
  case button::down:
    data_[0] = 0x01 << 3;
    break;
  case button::up:
    data_[0] = 0x01 << 4;
    break;
  default:
    data_[0] = 0;
    break;
  }

  duration = std::clamp(duration, 0ms, std::chrono::milliseconds(maximum_mask_duration));
  const auto ms = static_cast<std::uint16_t>(duration.count());
  data_[1] = static_cast<std::uint8_t>((ms >> 8) & 0xFF);
  data_[2] = static_cast<std::uint8_t>((ms >> 0) & 0xFF);

  while (true) {
    boost::system::error_code ec;
    socket_.send_to(boost::asio::buffer(data_.data(), 3), endpoint_, {}, ec);
    if (ec != boost::system::errc::resource_unavailable_try_again) {
      break;
    }
    std::this_thread::sleep_for(1us);
  }
}

void hid::move(std::int16_t x, std::int16_t y) noexcept
{
  data_[0] = static_cast<std::uint8_t>(static_cast<uint16_t>(x) & 0xFF);
  data_[1] = static_cast<std::uint8_t>(static_cast<uint16_t>(x) >> 8 & 0xFF);
  data_[2] = static_cast<std::uint8_t>(static_cast<uint16_t>(y) & 0xFF);
  data_[3] = static_cast<std::uint8_t>(static_cast<uint16_t>(y) >> 8 & 0xFF);

  while (true) {
    boost::system::error_code ec;
    socket_.send_to(boost::asio::buffer(data_.data(), 4), endpoint_, {}, ec);
    if (ec != boost::system::errc::resource_unavailable_try_again) {
      break;
    }
    std::this_thread::sleep_for(1us);
  }
}

}  // namespace horus