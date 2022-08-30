#include <anubis/client.hpp>
#include <horus/log.hpp>
#include <algorithm>
#include <chrono>
#include <exception>
#include <execution>
#include <filesystem>
#include <cstdlib>

#include <windows.h>
#include <dinput.h>
#include <dinputd.h>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

int main(int argc, char* argv[])
{
  horus::logger logger("C:/OBS/horus.log", true);
  try {
    using clock = std::chrono::high_resolution_clock;
    clock::time_point tp1;

    anubis::client client;

    auto thread = std::thread([&]() {
      LPDIRECTINPUT8 input{ nullptr };
      LPDIRECTINPUTDEVICE8 device{ nullptr };
      DIMOUSESTATE2 state{};

      auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
      if (FAILED(hr)) {
        throw std::runtime_error("could not initialize com library");
      }
      hr = DirectInput8Create(
        GetModuleHandle(nullptr),
        DIRECTINPUT_VERSION,
        IID_IDirectInput8,
        reinterpret_cast<LPVOID*>(&input),
        nullptr);
      if (FAILED(hr)) {
        throw std::runtime_error("could not initialize direct input");
      }
      hr = input->CreateDevice(GUID_SysMouse, &device, nullptr);
      if (FAILED(hr)) {
        throw std::runtime_error("could not create mouse device");
      }
      hr = device->SetDataFormat(&c_dfDIMouse2);
      if (FAILED(hr)) {
        throw std::runtime_error("could not set mouse data format");
      }
      hr = device->SetCooperativeLevel(GetConsoleWindow(), DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
      if (FAILED(hr)) {
        throw std::runtime_error("could not set mouse cooperative level");
      }
      hr = device->Acquire();
      if (FAILED(hr)) {
        throw std::runtime_error("could not acquire mouse");
      }
      while (true) {
        hr = device->GetDeviceState(sizeof(state), &state);
        if (SUCCEEDED(hr) && state.rgbButtons[0]) {
          tp1 = clock::now();
          return;
        }
      }
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    clock::time_point tp0 = clock::now();
    client.inject(0x01);
    thread.join();

    using duration = std::chrono::duration<double>;
    horus::log("{} ms", std::chrono::duration_cast<duration>(tp1 - tp0).count());
    
  }
  catch (const std::exception& e) {
    horus::log("error: {}", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}