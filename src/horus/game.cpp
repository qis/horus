#include "game.hpp"
#include <horus/timer.hpp>
#include <windows.h>

namespace horus::game {

boost::asio::awaitable<void> monitor(std::atomic_bool& focus) noexcept
{
  const auto executor = co_await boost::asio::this_coro::executor;
  timer timer{ executor };
  HWND game = nullptr;
  clock::time_point update{};
  while (true) {
    timer.expires_from_now(std::chrono::milliseconds(100));
    if (const auto [ec] = co_await timer.async_wait(); ec) {
      co_return;
    }
    if (const auto now = clock::now(); now > update) {
      game = FindWindow("TankWindowClass", "Overwatch");
      update = now + std::chrono::seconds(1);
    }
    focus.store(game ? game == GetForegroundWindow() : false, std::memory_order_release);
  }
  co_return;
}

}  // namespace horus::game