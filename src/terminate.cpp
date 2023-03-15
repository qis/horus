#include <tlhelp32.h>
#include <windows.h>
#include <chrono>
#include <string_view>
#include <thread>

DWORD find_process(std::string_view executable) noexcept
{
  DWORD process = 0;
  if (const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL)) {
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);
    if (Process32First(snapshot, &entry)) {
      while (Process32Next(snapshot, &entry)) {
        if (std::string_view(entry.szExeFile) == executable) {
          process = entry.th32ProcessID;
          break;
        }
      }
    }
    CloseHandle(snapshot);
  }
  return process;
}

HWND find_window(DWORD process) noexcept
{
  struct info {
    DWORD process;
    HWND window{ nullptr };
  } data{ process };
  EnumWindows(
    [](HWND window, LPARAM lparam) -> BOOL {
      DWORD process = 0;
      GetWindowThreadProcessId(window, &process);
      const auto data = reinterpret_cast<info*>(lparam);
      if (process == data->process) {
        if (!GetWindow(window, GW_OWNER) && IsWindowVisible(window)) {
          data->window = window;
          return FALSE;
        }
      }
      return TRUE;
    },
    reinterpret_cast<LPARAM>(&data));
  return data.window;
}

int main(int argc, char* argv[])
{
  constexpr std::string_view executable{ "obs64.exe" };
  const auto process = find_process(executable);
  if (!process) {
    return EXIT_SUCCESS;
  }
  if (const auto window = find_window(process)) {
    PostMessage(window, WM_CLOSE, 0, 0);
  }
  const auto tp0 = std::chrono::system_clock::now();
  do {
    if (!find_process(executable)) {
      return EXIT_SUCCESS;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  } while (std::chrono::system_clock::now() - tp0 < std::chrono::seconds(5));
  return EXIT_FAILURE;
}