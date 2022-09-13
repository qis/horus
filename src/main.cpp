#include <anubis/client.hpp>
#include <chrono>
#include <iostream>
#include <thread>

int usage()
{
  std::cerr << "usage: horus.exe L|R|M|U|D|E|S|P [seconds]" << std::endl;
  return EXIT_FAILURE;
}

static HHOOK keyboard_hook = nullptr;

static LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wparam, LPARAM lparam)
{
  const auto ks = reinterpret_cast<LPKBDLLHOOKSTRUCT>(lparam);
  switch (wparam) {
  case WM_KEYDOWN:
    std::cout << "KD: ";
    break;
  case WM_KEYUP:
    std::cout << "KU: ";
    break;
  case WM_SYSKEYDOWN:
    std::cout << "SD: ";
    break;
  case WM_SYSKEYUP:
    std::cout << "SU: ";
    break;
  }
  std::cout << ks->vkCode << std::endl;
  return CallNextHookEx(keyboard_hook, code, wparam, lparam);
}

int main(int argc, char* argv[])
{
  try {
    keyboard_hook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProc, nullptr, 0);
    if (!keyboard_hook) {
      throw std::runtime_error("could not add low level keyboard hook");
    }
    std::cout << "ready" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));
    std::cout << "done" << std::endl;
    UnhookWindowsHookEx(keyboard_hook);
    return EXIT_SUCCESS;
    if (argc < 2) {
      return usage();
    }
    uint8_t mask = 0;
    switch (argv[1][0]) {
    case 'L':
      mask = anubis::button::left;
      break;
    case 'R':
      mask = anubis::button::right;
      break;
    case 'M':
      mask = anubis::button::middle;
      break;
    case 'U':
      mask = anubis::button::up;
      break;
    case 'D':
      mask = anubis::button::down;
      break;
    case 'E':
      mask = anubis::button::e;
      break;
    case 'S':
      mask = anubis::button::shift;
      break;
    case 'P':
      mask = anubis::button::space;
      break;
    default:
      return usage();
    }
    if (argc > 2) {
      const auto seconds = atoi(argv[2]);
      if (seconds < 0 || seconds > 60) {
        std::cerr << "seconds must be between 0 and 60" << std::endl;
        return EXIT_FAILURE;
      }
      std::this_thread::sleep_for(std::chrono::seconds(seconds));
    }
    anubis::client client;
    
    client.mask(mask, std::chrono::milliseconds(7));
  }
  catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}