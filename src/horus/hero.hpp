#pragma once
#include <horus/eye.hpp>
#include <horus/hid.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

namespace horus::hero {

using token = decltype(boost::asio::as_tuple(boost::asio::use_awaitable));
using timer = decltype(token::as_default_on(boost::asio::steady_timer({})));

class base {
public:
  base(boost::asio::any_io_executor executor, eye& eye, hid& hid) noexcept :
    eye_(eye), hid_(hid), timer_(executor)
  {}

  base(base&& other) = default;
  base(const base& other) = delete;

  base& operator=(base&& other) = default;
  base& operator=(const base& other) = delete;

  virtual ~base() = default;

  virtual const char* name() const noexcept = 0;

  virtual bool scan(clock::time_point tp) noexcept
  {
    return false;
  }

  virtual bool draw(cv::Mat& overlay) noexcept
  {
    return false;
  }

  virtual boost::asio::awaitable<void> update() noexcept
  {
    co_return;
  }

protected:
  eye& eye_;

  boost::asio::awaitable<void> sleep(boost::asio::steady_timer::duration duration) noexcept
  {
    timer_.expires_from_now(duration);
    co_await timer_.async_wait();
    co_return;
  }

  hid::mouse movement() noexcept
  {
    return hid_.movement();
  }

  constexpr bool up(button button) const noexcept
  {
    return hid_.up(button);
  }

  constexpr bool down(button button) const noexcept
  {
    return hid_.down(button);
  }

  constexpr bool pressed(button button) const noexcept
  {
    return hid_.pressed(button);
  }

  constexpr bool released(button button) const noexcept
  {
    return hid_.released(button);
  }

  constexpr bool up(key key) const noexcept
  {
    return hid_.up(key);
  }

  constexpr bool down(key key) const noexcept
  {
    return hid_.down(key);
  }

  constexpr bool pressed(key key) const noexcept
  {
    return hid_.pressed(key);
  }

  constexpr bool released(key key) const noexcept
  {
    return hid_.released(key);
  }

  void mask(button button, std::chrono::milliseconds duration = std::chrono::milliseconds(0)) noexcept
  {
    hid_.mask(button, duration);
  }

  void move(std::int16_t x, std::int16_t y) noexcept
  {
    hid_.move(x, y);
  }

  boost::asio::awaitable<void> move(int x, int y, std::size_t steps, std::chrono::milliseconds delay) noexcept
  {
    const auto sx = x / static_cast<int>(steps);
    const auto sy = y / static_cast<int>(steps);
    assert(std::abs(sx) < static_cast<int>(std::numeric_limits<std::int16_t>::max()));
    assert(std::abs(sy) < static_cast<int>(std::numeric_limits<std::int16_t>::max()));
    for (std::size_t i = 0; i < steps; i++) {
      const auto mx = static_cast<std::int16_t>(std::abs(x) > std::abs(sx) ? sx : x);
      const auto my = static_cast<std::int16_t>(std::abs(y) > std::abs(sy) ? sy : y);
      if (mx || my) {
        move(mx, my);
        co_await sleep(delay);
      }
    }
    co_return;
  }

private:
  hid& hid_;
  timer timer_;
};

HORUS_API std::shared_ptr<base> next_support_hero(
  boost::asio::any_io_executor context,
  std::shared_ptr<base> hero,
  eye& eye,
  hid& hid);

HORUS_API std::shared_ptr<base> next_damage_hero(
  boost::asio::any_io_executor context,
  std::shared_ptr<base> hero,
  eye& eye,
  hid& hid);

}  // namespace horus::hero