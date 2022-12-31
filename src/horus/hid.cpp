#include "hid.hpp"
#include <horus/log.hpp>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

namespace horus {

hid::hid() noexcept
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

  if (!ewd.hwnd) {
    log("could not find current process window handle");
    return;
  }

  HRESULT hr = 0;

  hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(hr)) {
    log("could not initialize com library");
    return;
  }

  hr = DirectInput8Create(
    GetModuleHandle(nullptr),
    DIRECTINPUT_VERSION,
    IID_IDirectInput8,
    reinterpret_cast<LPVOID*>(&input_),
    nullptr);
  if (FAILED(hr)) {
    log("could not initialize direct input");
    input_ = nullptr;
    return;
  }

  hr = input_->CreateDevice(GUID_SysKeyboard, &keybd_, nullptr);
  if (SUCCEEDED(hr)) {
    hr = keybd_->SetDataFormat(&c_dfDIKeyboard);
    if (FAILED(hr)) {
      log("could not set keyboard data format");
    }
    hr = keybd_->SetCooperativeLevel(ewd.hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    if (FAILED(hr)) {
      log("could not set keyboard cooperative level");
    }
    hr = keybd_->Acquire();
    if (FAILED(hr)) {
      log("could not acquire keyboard");
    }
  } else {
    log("could not create keyboard device");
    keybd_ = nullptr;
  }

  hr = input_->CreateDevice(GUID_SysMouse, &mouse_, nullptr);
  if (SUCCEEDED(hr)) {
    hr = mouse_->SetDataFormat(&c_dfDIMouse2);
    if (FAILED(hr)) {
      log("could not set mouse data format");
    }
    hr = mouse_->SetCooperativeLevel(ewd.hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    if (FAILED(hr)) {
      log("could not set mouse cooperative level");
    }
    hr = mouse_->Acquire();
    if (FAILED(hr)) {
      log("could not acquire mouse");
    }
  } else {
    log("could not create mouse device");
    mouse_ = nullptr;
  }
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

bool hid::get(keybd& state) noexcept
{
  if (!keybd_) {
    return false;
  }
  const auto hr = keybd_->GetDeviceState(sizeof(keybd_state_), keybd_state_);
  if (SUCCEEDED(hr)) {
    state.enter = keybd_state_[DIK_RETURN] & 0x80 ? true : false;
    state.menu = keybd_state_[DIK_LMENU] & 0x80 ? true : false;
    return true;
  }
  keybd_->Acquire();
  return false;
}

bool hid::get(mouse& state) noexcept
{
  if (!mouse_) {
    return false;
  }
  const auto hr = mouse_->GetDeviceState(sizeof(mouse_state_), &mouse_state_);
  if (SUCCEEDED(hr)) {
    state.left = mouse_state_.rgbButtons[0] != 0;
    state.right = mouse_state_.rgbButtons[1] != 0;
    state.middle = mouse_state_.rgbButtons[2] != 0;
    state.down = mouse_state_.rgbButtons[3] != 0;
    state.up = mouse_state_.rgbButtons[4] != 0;
    state.dx = mouse_state_.lX;
    state.dy = mouse_state_.lY;
    return true;
  }
  mouse_->Acquire();
  state.dx = 0;
  state.dy = 0;
  return false;
}

}  // namespace horus