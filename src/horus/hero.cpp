#include "hero.hpp"
#include <format>
#include <map>
#include <optional>

namespace horus::hero {
namespace {

constexpr auto mf = 0.0442846f;

__forceinline std::pair<float, float> mouse2view(float mx, float my) noexcept
{
  return { mx * mf, my * mf };
}

constexpr std::pair<std::int16_t, std::int16_t> view2mouse(float mx, float my) noexcept
{
  return { static_cast<std::int16_t>(mx / mf), static_cast<std::int16_t>(my / mf) };
}

__forceinline std::optional<cv::Point> centroid(const eye::polygon& polygon) noexcept
{
  const auto moments = cv::moments(polygon);
  if (moments.m00 == 0.0) {
    return std::nullopt;
  }
  return cv::Point(
    static_cast<int>(moments.m10 / moments.m00),
    static_cast<int>(moments.m01 / moments.m00));
}

void connect_view_points(std::vector<cv::Point>& points, cv::Point p1, cv::Point p2, int skip = 0) noexcept
{
  auto x1 = p1.x;
  auto y1 = p1.y;
  auto x2 = p2.x;
  auto y2 = p2.y;

  auto dx = std::abs(x2 - x1);
  auto dy = std::abs(y2 - y1);

  points.clear();
  if (!dx && !dy) {
    points.push_back(p1);
    return;
  }

  const auto steep = dy > dx;

  if (steep) {
    std::swap(x1, y1);
    std::swap(x2, y2);
    std::swap(dx, dy);
  }

  if (x1 > x2) {
    std::swap(x1, x2);
    std::swap(y1, y2);
  }

  const auto ystep = y1 < y2 ? 1 : -1;

  auto lx = x1;
  auto ly = y1;
  if (steep) {
    points.emplace_back(ly, lx);
  } else {
    points.emplace_back(lx, ly);
  }

  auto y = y1;
  auto error = dx / 2.0f;
  for (auto x = x1; x <= x2; x++) {
    if (skip) {
      if (std::abs(x - lx) > skip || std::abs(y - ly) > skip) {
        if (steep) {
          points.emplace_back(y, x);
        } else {
          points.emplace_back(x, y);
        }
        lx = x;
        ly = y;
      }
    } else {
      if (steep) {
        points.emplace_back(y, x);
      } else {
        points.emplace_back(x, y);
      }
    }
    error -= dy;
    if (error < 0) {
      y += ystep;
      error += dx;
    }
  }
}

}  // namespace

using namespace std::chrono_literals;

class ana : public base {
public:
  // Controls
  // ========
  // + HERO
  //   RELATIVE AIM SENSITIVITY WHILE ZOOMED: 46.00%
  //   RECOIL RECOVERY AIM COMPENSATION: OFF
  //   NANO BOOST REQUIRES TARGET CONFIRMATION: ON
  // + WEAPONS & ABILITIES
  //   PRIMARY FIRE: LEFT MOUSE BUTTON | MOUSE BUTTON 5

  ana(const boost::asio::any_io_executor& executor, eye& eye, hid& hid) noexcept :
    base(executor, eye, hid)
  {}

  const char* name() const noexcept override
  {
    return "ana";
  }

  void scan(float mx, float my) noexcept override
  {
    // Update mouse movement.
    std::tie(mx, my) = mouse2view(mx, my);
    mc_.x = eye::tc.x + static_cast<int>(mx);
    mc_.y = eye::tc.y + static_cast<int>(my);

    // Acquire target.
    target_ = false;
    for (const auto& target : eye_.targets()) {
      if (cv::pointPolygonTest(target.hull, mc_, true) > 1.0) {
        target_ = true;
        break;
      }
    }

    // Handle lockout.
    const auto now = clock::now();
    if (down(button::left)) {
      lockout_ = now + 128ms;
    } else if (pressed(button::right)) {
      lockout_ = now + 300ms;
    }
    if (now < lockout_) {
      return;
    }

    // Fire if a target was acquired.
    if (target_) {
      mask(button::up, 16ms);
      lockout_ = now + 128ms;
    }
  }

private:
  cv::Point mc_{};
  bool target_{ false };
  clock::time_point lockout_{};
};

class brigitte : public base {
public:
  // Controls
  // ========
  // + RETICLE
  //   + TYPE: CIRCLE
  //     SHOW ACCURACY: OFF
  //     CENTER GAP: 60
  // + MOVEMENT
  //   JUMP: SPACE | MOUSE BUTTON 5

  brigitte(boost::asio::any_io_executor executor, eye& eye, hid& hid) noexcept :
    base(executor, eye, hid)
  {}

  const char* name() const noexcept override
  {
    return "brigitte";
  }

  boost::asio::awaitable<void> update() noexcept override
  {
    // Perform bash jump when button down is pressed.
    if (pressed(button::down)) {
      mask(button::up, 64ms);
      co_await sleep(40ms);
      mask(button::right, 128ms);
      co_await sleep(40ms);
      mask(button::left, 32ms);
    }
    co_return;
  }
};

class mercy : public base {
public:
  // Controls
  // ========
  // + HERO
  //   TOGGLE BEAM CONNECTION: ON
  //   GUARDIAN ANGEL TARGET PRIORITY: PREFER FACING TARGET
  // + MOVEMENT
  //   CROUCH: LCONTROL | MOUSE BUTTON 4
  //   JUMP: MOUSE BUTTON 5

  mercy(boost::asio::any_io_executor executor, eye& eye, hid& hid) noexcept :
    base(executor, eye, hid)
  {}

  const char* name() const noexcept override
  {
    return "mercy";
  }

  boost::asio::awaitable<void> update() noexcept override
  {
    // Disable enter, escape, windows key and alt + tab.
    const auto alt_tab = down(key::alt) && down(key::tab);
    if (down(key::enter) || down(key::escape) || down(key::win) || alt_tab) {
      if (enabled_) {
        mask(button::up);
        glide_update_ = {};
        glide_ = false;
      }
      enabled_ = false;
      co_return;
    }

    // Enable on alt.
    if (down(key::alt)) {
      enabled_ = true;
    }

    // Skip when not enabled.
    if (!enabled_) {
      co_return;
    }

    // Stop Glide on b.
    if (glide_ && down(key::b)) {
      mask(button::up);
      glide_ = false;
    }

    // Get current time.
    const auto now = clock::now();

    // Enter Valkyrie mode when q is pressed.
    if (pressed(key::q)) {
      valkyrie_timeout_ = now + 15s;
      valkyrie_ = true;
      if (up(key::space)) {
        mask(button::up);
        glide_ = false;
      }
    }

    // Exit Valkyrie mode when alt is pressed or timeout reached.
    if (valkyrie_ && (pressed(key::alt) || now > valkyrie_timeout_)) {
      valkyrie_ = false;
      glide_ = true;
      glide_update_ = {};
    }

    // Reset Glide override when shift is pressed.
    if (pressed(key::shift)) {
      glide_override_ = false;
    }

    // Perform Super Jump when space is pressed while shift is down.
    if (down(key::shift) && pressed(key::space)) {
      mask(button::up);
      mask(button::down, 16ms);
      glide_update_ = now + 32ms;
      glide_override_ = true;
      glide_ = true;
      co_return;
    }

    // Start Glide when shift is released or s is pressed while shift is down (unless overridden).
    if (!glide_override_ && (released(key::shift) || (down(key::shift) && pressed(key::s)))) {
      mask(button::up);
      glide_update_ = now + 32ms;
      glide_ = true;
      co_return;
    }

    // Start Glide when space is pressed while shift is up.
    if (up(key::shift) && pressed(key::space)) {
      mask(button::up);
      glide_update_ = now + 32ms;
      glide_ = true;
      co_return;
    }

    // Reset Glide override when space is released while shift is down.
    if (down(key::shift) && released(key::space)) {
      glide_override_ = false;
    }

    // Stop Glide when space is released while shift is up.
    if (up(key::shift) && released(key::space)) {
      if (glide_override_) {
        glide_override_ = false;
      } else {
        mask(button::up);
        glide_ = false;
      }
      co_return;
    }

    // Handle Glide.
    if (glide_ && now > glide_update_) {
      mask(button::up, valkyrie_ && up(key::space) ? 16ms : 2s);
      glide_update_ = now + 1s;
      if (valkyrie_ && up(key::space)) {
        glide_ = false;
      }
    }
    co_return;
  }

private:
  bool enabled_{ true };

  bool glide_{ false };
  bool glide_override_{ false };
  clock::time_point glide_update_{};

  bool valkyrie_{ false };
  clock::time_point valkyrie_timeout_{};
};

class bastion : public base {
public:
  // Controls
  // ========
  // + RETICLE: CIRCLE
  // + MOVEMENT
  //   JUMP: SPACE | MOUSE BUTTON 5

  bastion(boost::asio::any_io_executor executor, eye& eye, hid& hid) noexcept :
    base(executor, eye, hid)
  {}

  const char* name() const noexcept override
  {
    return "bastion";
  }

  boost::asio::awaitable<void> update() noexcept override
  {
    // Perform grenade jump when alt is pressed.
    if (pressed(key::alt)) {
      co_await move(0, 8000, 8, 8ms);
      mask(button::right, 16ms);
      co_await sleep(550ms);
      mask(button::up, 16ms);
      co_await sleep(32ms);
      co_await move(0, -6200, 8, 8ms);
    }
    co_return;
  }
};

class reaper : public base {
public:
  // Controls
  // ========
  // + RETICLE: CIRCLE
  // + WEAPONS & ABILITIES
  //   PRIMARY FIRE: LEFT MOUSE BUTTON | MOUSE BUTTON 5

  static constexpr double spread_radius = 40.0;

  reaper(boost::asio::any_io_executor executor, eye& eye, hid& hid) noexcept :
    base(executor, eye, hid)
  {
    points_.reserve(std::max(eye::tw / 2, eye::th / 2));
  }

  const char* name() const noexcept override
  {
    return "reaper";
  }

  void scan(float mx, float my) noexcept override
  {
    // Update mouse movement.
    std::tie(mx_, my_) = mouse2view(mx, my);
    mc_.x = eye::tc.x + static_cast<int>(mx_ * 2.0f);
    mc_.y = eye::tc.y + static_cast<int>(my_ * 2.0f);

    // Create points between mouse movement and center of view.
    connect_view_points(points_, mc_, eye::tc, 1);

    // Acquire target.
    target_ = true;
    for (const auto& target : eye_.targets()) {
      for (const auto& point : points_) {
        if (cv::pointPolygonTest(target.hull, point, false) > 0.0) {
          goto acquired;
        }
        const auto center = centroid(target.hull);
        if (center && cv::norm(*center - mc_) < spread_radius / 2.0) {
          goto acquired;
        }
      }
    }
    target_ = false;
  acquired:

    // Handle lockout and trigger key.
    const auto now = clock::now();
    if (down(button::left)) {
      lockout_ = now + 128ms;
    }
    if (now < lockout_ || !down(button::right)) {
      return;
    }

    // Adjust crosshair and fire.
    if (target_) {
      if (std::abs(mx_) > 4.0f || std::abs(my_) > 4.0f) {
        auto vx = -mx_;
        if (std::abs(vx) > 8) {
          vx = vx < 0.0f ? -8.0f : 8.0f;
        }
        auto vy = -my_;
        if (std::abs(vy) > 8) {
          vy = vy < 0.0f ? -8.0f : 8.0f;
        }
        const auto md = view2mouse(vx, vy);
        move(md.first, md.second);
      }
      mask(button::up, 16ms);
      lockout_ = now + 128ms;
    }
  }

  bool draw(cv::Mat& overlay) noexcept override
  {
    info_.clear();
    eye_.draw_hulls(overlay);
    eye_.draw(overlay, mc_, target_ ? 0xD50000FF : 0x00B0FFFF);
    std::format_to(std::back_inserter(info_), "{:05.1f} x | {:05.1f} y", mx_, my_);
    eye_.draw(overlay, { 2, eye::th - 40 }, info_);
    return false;
  }

  boost::asio::awaitable<void> update() noexcept override
  {
    if (pressed(key::f8)) {
      move(700, 0);
    }
    co_return;
  }

private:
  std::string info_;
  float mx_{};
  float my_{};
  cv::Point mc_{};
  std::vector<cv::Point> points_;
  bool target_{ false };
  clock::time_point lockout_{};
};

class soldier : public base {
public:
  // Controls
  // ========
  // + MOVEMENT
  //   JUMP: SPACE | MOUSE BUTTON 5
  // + WEAPONS & ABILITIES
  //   ABILITY 1: LSHIFT | MOUSE BUTTON 4

  soldier(boost::asio::any_io_executor executor, eye& eye, hid& hid) noexcept :
    base(executor, eye, hid)
  {}

  const char* name() const noexcept override
  {
    return "soldier";
  }

  boost::asio::awaitable<void> update() noexcept override
  {
    // How much ammo is available.
    constexpr auto ammo = 30;

    // After how many ammo changes the recoil increases.
    constexpr auto ammo_increase = 5;

    // How long it takes for the ammo value to change.
    constexpr auto ammo_interval = 104ms;

    // How long to wait after left button down and recoil copmensation.
    constexpr auto compensate_delay = ammo_interval * 2;

    // How long to wait between recoil compensation injects.
    constexpr auto compensate_interval = 16ms;

    // How much to move the mouse for each ammo value change before and after increase.
    constexpr auto compensate_ammo0 = view2mouse(0.0, 4.0).second;
    constexpr auto compensate_ammo1 = view2mouse(0.0, 6.2).second;

    // How often to move the mouse between ammo value changes.
    constexpr auto compensate_count = (ammo_interval - compensate_interval) / compensate_interval;

    // Mouse movement per inject before and after increase.
    constexpr auto compensate_move0 = compensate_ammo0 / compensate_count;
    constexpr auto compensate_move1 = compensate_ammo1 / compensate_count;

    // Compensate for recoil.
    const auto now = clock::now();
    if (up(button::left) || down(key::r) || down(key::alt)) {
      compensate_ = false;
    } else if (pressed(button::left)) {
      compensate_increase_ = now + ammo_interval * ammo_increase;
      compensate_ammo_ = now + ammo_interval * ammo + 180ms;
      compensate_next_ = now + compensate_delay;
      compensate_ = true;
    } else if (compensate_ && now > compensate_next_ && !down(button::right)) {
      move(0, now > compensate_increase_ ? compensate_move1 : compensate_move0);
      compensate_next_ = now + compensate_interval;
      compensate_ = now < compensate_ammo_;
    }

    // Record burst start time point.
    if (pressed(button::left)) {
      burst_start_.store(clock::now(), std::memory_order_release);
      burst_.store(true, std::memory_order_release);
    } else if (released(button::left)) {
      burst_.store(false, std::memory_order_release);
    } else if (!down(button::left)) {
      burst_.store(false, std::memory_order_relaxed);
    }

    // Perform rocket jump when alt is pressed.
    if (!compensate_ && pressed(key::alt)) {
      co_await move(0, 8000, 8, 8ms);
      mask(button::up, 16ms);
      mask(button::right, 16ms);
      co_await sleep(64ms);
      mask(button::down, 16ms);
      co_await move(0, -5600, 8, 8ms);
    }
    co_return;
  }

  bool draw(cv::Mat& overlay) noexcept override
  {
    if (burst_.load(std::memory_order_acquire)) {
      info_.clear();
      const auto start = burst_start_.load(std::memory_order_acquire);
      const auto duration = duration_cast<milliseconds<float>>(clock::now() - start);
      std::format_to(std::back_inserter(info_), "{:08.3f} ms", duration.count());
      eye_.draw(overlay, { 2, eye::th - 40 }, info_);
    }
    return false;
  }

private:
  bool compensate_{ false };
  clock::time_point compensate_increase_{};
  clock::time_point compensate_next_{};
  clock::time_point compensate_ammo_{};

  std::atomic_bool burst_{ false };
  std::atomic<clock::time_point> burst_start_;
  std::string info_;
};

using hero_factory = std::function<std::shared_ptr<base>(boost::asio::any_io_executor, eye&, hid&)>;

std::shared_ptr<base> next_hero(
  const std::map<std::string, hero_factory>& hero_factories,
  boost::asio::any_io_executor executor,
  std::shared_ptr<base> hero,
  eye& eye,
  hid& hid)
{
  const auto hero_factories_begin = hero_factories.begin();
  const auto hero_factories_end = hero_factories.end();
  std::shared_ptr<base> next;
  if (hero) {
    for (auto it = hero_factories_begin; it != hero_factories_end; ++it) {
      if (it->first == hero->name()) {
        if (++it != hero_factories_end) {
          next = it->second(executor, eye, hid);
        }
        break;
      }
    }
  }
  if (!next) {
    next = hero_factories_begin->second(executor, eye, hid);
  }
  return next;
}

std::shared_ptr<base> next_support_hero(
  boost::asio::any_io_executor executor,
  std::shared_ptr<base> hero,
  eye& eye,
  hid& hid)
{
  static const std::map<std::string, hero_factory> hero_factories{
    {
      "ana",
      [](boost::asio::any_io_executor executor, horus::eye& eye, horus::hid& hid) {
        return std::make_shared<ana>(executor, eye, hid);
      },
    },
    {
      "brigitte",
      [](boost::asio::any_io_executor executor, horus::eye& eye, horus::hid& hid) {
        return std::make_shared<brigitte>(executor, eye, hid);
      },
    },
    {
      "mercy",
      [](boost::asio::any_io_executor executor, horus::eye& eye, horus::hid& hid) {
        return std::make_shared<mercy>(executor, eye, hid);
      },
    },
  };
  return next_hero(hero_factories, executor, hero, eye, hid);
}

std::shared_ptr<base> next_damage_hero(
  boost::asio::any_io_executor executor,
  std::shared_ptr<base> hero,
  eye& eye,
  hid& hid)
{
  static const std::map<std::string, hero_factory> hero_factories{
    {
      "bastion",
      [](boost::asio::any_io_executor executor, horus::eye& eye, horus::hid& hid) {
        return std::make_shared<bastion>(executor, eye, hid);
      },
    },
    {
      "reaper",
      [](boost::asio::any_io_executor executor, horus::eye& eye, horus::hid& hid) {
        return std::make_shared<reaper>(executor, eye, hid);
      },
    },
    {
      "soldier",
      [](boost::asio::any_io_executor executor, horus::eye& eye, horus::hid& hid) {
        return std::make_shared<soldier>(executor, eye, hid);
      },
    },
  };
  return next_hero(hero_factories, executor, hero, eye, hid);
}

}  // namespace horus::hero