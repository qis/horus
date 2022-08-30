#include "mouse.hpp"
#include <horus/log.hpp>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

namespace horus {

mouse::mouse() noexcept
{
  struct enum_windows_data {
    DWORD pid = GetCurrentProcessId();
    HWND hwnd = nullptr;
  } ewd;

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

  if (ewd.hwnd) {
    auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr)) {
      hr = DirectInput8Create(
        GetModuleHandle(nullptr),
        DIRECTINPUT_VERSION,
        IID_IDirectInput8,
        reinterpret_cast<LPVOID*>(&input_),
        nullptr);
      if (SUCCEEDED(hr)) {
        hr = input_->CreateDevice(GUID_SysMouse, &device_, nullptr);
        if (SUCCEEDED(hr)) {
          hr = device_->SetDataFormat(&c_dfDIMouse2);
          if (FAILED(hr)) {
            log("could not set mouse data format");
          }
          hr = device_->SetCooperativeLevel(ewd.hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
          if (FAILED(hr)) {
            log("could not set mouse cooperative level");
          }
          hr = device_->Acquire();
          if (FAILED(hr)) {
            log("could not acquire mouse");
          }
        } else {
          log("could not create mouse device");
          device_ = nullptr;
          input_->Release();
        }
      } else {
        log("could not initialize direct input");
        input_ = nullptr;
      }
    } else {
      log("could not initialize com library");
    }
  } else {
    log("could not find current process window handle");
  }
}

mouse::~mouse()
{
  if (device_) {
    device_->Unacquire();
    device_->Release();
  }
  if (input_) {
    input_->Release();
  }
}

bool mouse::get(state& state) noexcept
{
  const auto hr = device_->GetDeviceState(sizeof(state_), &state_);
  if (SUCCEEDED(hr)) {
    state.mx = state_.lX;
    state.my = state_.lY;
    state.bl = state_.rgbButtons[0] != 0;
    state.br = state_.rgbButtons[1] != 0;
    state.bm = state_.rgbButtons[2] != 0;
    state.bd = state_.rgbButtons[3] != 0;
    state.bu = state_.rgbButtons[4] != 0;
    return true;
  }
  device_->Acquire();
  state.mx = 0;
  state.my = 0;
  return false;
}

}  // namespace horus