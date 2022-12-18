#include <rock/client.hpp>
#include <chrono>
#include <format>
#include <thread>
#include <cassert>
#include <cstdio>

std::atomic_bool g_active = false;

class looter {
public:
  static constexpr std::int16_t x180 = 19582;

  enum class function {
    buy_stack,
    craft_items,
    disassemble_items,
    powerlevel_breach_protocol,
    loot_exploit,
  };

  using clock = std::chrono::high_resolution_clock;

  looter(function function) noexcept : function_(function)
  {
    thread_ = std::thread([this]() {
      std::puts("enabled");
      run();
    });
  }

  ~looter()
  {
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) {
      thread_.join();
    }
  }

private:
  bool sleep(std::chrono::milliseconds duration) noexcept
  {
    auto now = clock::now();
    const auto end = now + duration;
    while (now + std::chrono::milliseconds(20) < end && !stop_.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      now = clock::now();
    }
    if (now < end && !stop_.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(end - now);
    }
    return !stop_.load(std::memory_order_relaxed);
  }

  void down(WORD key) noexcept
  {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = key;
    SendInput(1, &input, sizeof(input));
  }

  void up(WORD key) noexcept
  {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    input.ki.wVk = key;
    SendInput(1, &input, sizeof(input));
  }

  bool press(WORD key, std::chrono::milliseconds duration = {}) noexcept
  {
    down(key);
    const auto ready = sleep(duration);
    up(key);
    return ready;
  }

#define CALL_FUNCTION(name) \
  case function::name:      \
    if (!name()) {          \
      goto stop;            \
    }                       \
    break;

  void run() noexcept
  {
    using namespace std::chrono_literals;
    while (!stop_.load(std::memory_order_relaxed)) {
      switch (function_) {
        CALL_FUNCTION(buy_stack);
        CALL_FUNCTION(craft_items);
        CALL_FUNCTION(disassemble_items);
        CALL_FUNCTION(powerlevel_breach_protocol);
        CALL_FUNCTION(loot_exploit);
      }
      continue;
    stop:
      g_active.store(false, std::memory_order_release);
      break;
    }
    std::uint8_t set = 0;
    set |= rock::button::left;
    set |= rock::button::right;
    set |= rock::button::middle;
    set |= rock::button::down;
    set |= rock::button::up;
    mask(set, 0s);
    std::puts("disabled");
  }

#undef CALL_FUNCTION

  bool buy_stack() noexcept
  {
    using namespace std::chrono_literals;

    // Initiate buy operation.
    press(VK_RETURN, 10ms);
    if (!sleep(200ms)) {
      return false;
    }

    // Move cursor to top left of the screen.
    move(3, -1344, -512);
    if (!sleep(100ms)) {
      return false;
    }

    // Move cursor to slider.
    move(1, 1445, 660);
    if (!sleep(100ms)) {
      return false;
    }

    // Grab slider.
    mask(rock::button::left, 200ms);
    if (!sleep(100ms)) {
      return false;
    }

    // Move slider to the right.
    move(1, 630, 0);
    if (!sleep(100ms)) {
      return false;
    }

    // Release slider.
    press(VK_RETURN, 10ms);

    // Stop loop.
    return false;
  }

  bool craft_items() noexcept
  {
    using namespace std::chrono_literals;

    // Craft item.
    mask(rock::button::left, 850ms);
    if (!sleep(950ms)) {
      return false;
    }

    // Continue loop.
    return true;
  }

  bool disassemble_items() noexcept
  {
    using namespace std::chrono_literals;

    // Disassemble item.
    down('Z');
    if (!sleep(500ms)) {
      up('Z');
      return false;
    }
    up('Z');
    if (!sleep(100ms)) {
      return false;
    }

    // Continue loop.
    return true;
  }

  bool powerlevel_breach_protocol() noexcept
  {
    using namespace std::chrono_literals;

    // Connect to access point.
    press('F');
    if (!sleep(5s)) {
      return false;
    }

    // Auto-solve breach protocol.
    press(VK_OEM_5);
    if (!sleep(5s)) {
      return false;
    }

    // Open quickhack menu and select breach access point.
    down(VK_TAB);
    if (!sleep(1s)) {
      return false;
    }
    press('F');
    if (!sleep(100ms)) {
      up(VK_TAB);
      return false;
    }
    up(VK_TAB);
    if (!sleep(400ms)) {
      return false;
    }

    // Cancel breach process.
    press(VK_ESCAPE);
    if (!sleep(500ms)) {
      return false;
    }

    // Exit breach menu.
    press(VK_ESCAPE);
    if (!sleep(500ms)) {
      return false;
    }

    // Look away from the access point.
    move(1, x180, 0);
    if (!sleep(500ms)) {
      return false;
    }
    move(1, -x180, 0);

    // Wait for breach cooldown to finish.
    if (!sleep(15s)) {
      return false;
    }

    // Continue loop.
    return true;
  }

  bool loot_exploit() noexcept
  {
    // Key Bindings
    // + Exploration and Combat
    //   Move Forward: Up Mouse Button (5)
    //   Move Backward: Down Mouse Button (4)
    //   Primary Interaction: Middle Mouse Button
    using namespace std::chrono_literals;

    // Loot container.
    mask(rock::button::middle, 10ms);
    if (!sleep(100ms)) {
      return false;
    }

    // Move backward.
    mask(rock::button::down, 7s);
    if (!sleep(7s + 800ms)) {
      return false;
    }

    // Move forward.
    mask(rock::button::up, 7s + 15ms);
    if (!sleep(5s)) {
      return false;
    }

    // Loot container early.
    for (auto i = 0; i < 21; i++) {
      mask(rock::button::middle, 10ms);
      if (!sleep(100ms)) {
        return false;
      }
    }

    // Continue loop.
    return true;
  }

  void mask(std::uint8_t set, std::chrono::milliseconds duration) noexcept
  {
    client_.mask(set, duration);
  }

  void move(std::uint8_t bat, std::int16_t x, std::int16_t y) noexcept
  {
    client_.move(bat, x, y);
  }

  function function_;
  rock::client client_;
  std::thread thread_;
  std::atomic_bool stop_{ false };
};

std::unique_ptr<looter> g_looter;
static HHOOK g_hook = nullptr;

static LRESULT CALLBACK HookProc(int code, WPARAM wparam, LPARAM lparam)
{
  if (wparam == WM_KEYDOWN) {
    const auto code = reinterpret_cast<LPKBDLLHOOKSTRUCT>(lparam)->vkCode;
    if (code == VK_F6 || code == VK_F7 || code == VK_F8) {
      auto active = g_active.load();
      while (!g_active.compare_exchange_weak(active, !active)) {
      }
      if (!active) {
        switch (code) {
        case VK_F6:
          g_looter = std::make_unique<looter>(looter::function::buy_stack);
          break;
        case VK_F7:
          g_looter = std::make_unique<looter>(looter::function::craft_items);
          break;
        case VK_F8:
          g_looter = std::make_unique<looter>(looter::function::disassemble_items);
          break;
        }
      } else {
        g_looter.reset();
      }
    }
  }
  return CallNextHookEx(g_hook, code, wparam, lparam);
}

int main(int argc, char* argv[])
{
  try {
    g_hook = SetWindowsHookEx(WH_KEYBOARD_LL, HookProc, nullptr, 0);
    if (!g_hook) {
      throw std::runtime_error("Could nto create low level keyboard hook.");
    }
    MSG msg{};
    while (GetMessageA(&msg, nullptr, 0, 0)) {
    }
  }
  catch (const std::exception& e) {
    std::fputs(e.what(), stderr);
    std::fputs("\r\n", stderr);
    std::fflush(stderr);
    return EXIT_FAILURE;
  }
  if (g_hook) {
    UnhookWindowsHookEx(g_hook);
  }
  return EXIT_SUCCESS;
}