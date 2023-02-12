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

__forceinline cv::Point center(const cv::Rect& rect) noexcept
{
  return { rect.x + rect.width / 2, rect.y + rect.height / 2 };
}

__forceinline cv::Point centroid(const eye::polygon& polygon, double spread = 0.0) noexcept
{
  cv::Point point;
  const auto rect = cv::boundingRect(polygon);
  if (rect.width && rect.height) {
    const auto moments = cv::moments(polygon);
    if (moments.m00 != 0.0) {
      point.x = static_cast<int>(moments.m10 / moments.m00);
      point.y = static_cast<int>(moments.m01 / moments.m00);
    } else {
      point = center(rect);
    }
  } else {
    point = center(rect);
  }
  if (const auto top = rect.y + spread; top < point.y) {
    point.y = top;
  }
  return point;
}

void connect_view_points(std::vector<cv::Point>& points, cv::Point p0, cv::Point p1, int skip = 0) noexcept
{
  auto x0 = p0.x;
  auto y0 = p0.y;
  auto x1 = p1.x;
  auto y1 = p1.y;

  auto dx = std::abs(x1 - x0);
  auto dy = std::abs(y1 - y0);

  points.clear();
  if (!dx && !dy) {
    points.push_back(p0);
    return;
  }

  const auto steep = dy > dx;

  if (steep) {
    std::swap(x0, y0);
    std::swap(x1, y1);
    std::swap(dx, dy);
  }

  if (x0 > x1) {
    std::swap(x0, x1);
    std::swap(y0, y1);
  }

  const auto ystep = y0 < y1 ? 1 : -1;

  auto lx = x0;
  auto ly = y0;
  if (steep) {
    points.emplace_back(ly, lx);
  } else {
    points.emplace_back(lx, ly);
  }

  auto y = y0;
  auto error = dx / 2.0f;
  for (auto x = x0; x <= x1; x++) {
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

  static constexpr auto prediction_multiplier = 2.5;

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
    std::tie(vx_, vy_) = mouse2view(mx, my);
    vc_.x = eye::vc.x + static_cast<int>(vx_ * prediction_multiplier);
    vc_.y = eye::vc.y + static_cast<int>(vy_ * prediction_multiplier);

    // Create points between mouse movement and center of view.
    connect_view_points(points_, vc_, eye::vc, 1);

    // Acquire target.
    target_ = true;
    for (const auto& target : eye_.polygons()) {
      const auto rect = cv::boundingRect(target);
      const auto margin = std::max(1.0, rect.width / 16.0);
      for (const auto& point : points_) {
        if (cv::pointPolygonTest(target, point, true) > margin) {
          goto acquired;
        }
      }
    }
    target_ = false;
  acquired:

    // Set trigger.
    const auto scoped = down(button::right) && up(button::down);
    const auto manual = down(button::down) && up(button::right);
    trigger_ = scoped || manual;

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
    if (target_ && trigger_) {
      mask(button::up, 16ms);
      lockout_ = now + 128ms;
    }
  }

  bool draw(cv::Mat& overlay) noexcept override
  {
    info_.clear();
    eye_.draw_polygons(overlay);
    if (trigger_) {
      eye_.draw(overlay, vc_, target_ ? 0xD50000FF : 0x00B0FFFF);
    }
    std::format_to(std::back_inserter(info_), "{:05.1f} x | {:05.1f} y", vx_, vy_);
    eye_.draw(overlay, { 2, eye::vh - 40 }, info_);
    return false;
  }

private:
  float vx_{};
  float vy_{};
  cv::Point vc_{};
  bool target_{ false };
  bool trigger_{ false };
  std::vector<cv::Point> points_;
  clock::time_point lockout_{};
  std::string info_;
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

  static constexpr auto spread = 40.0;
  static constexpr auto prediction_multiplier = 2.5;
  static constexpr auto draw_centroids = false;

  reaper(boost::asio::any_io_executor executor, eye& eye, hid& hid) noexcept :
    base(executor, eye, hid)
  {
    points_.reserve(std::max(eye::vw / 2, eye::vh / 2));
  }

  const char* name() const noexcept override
  {
    return "reaper";
  }

  void scan(float mx, float my) noexcept override
  {
    // Update mouse movement.
    std::tie(vx_, vy_) = mouse2view(mx, my);
    vc_.x = eye::vc.x + static_cast<int>(vx_ * prediction_multiplier);
    vc_.y = eye::vc.y + static_cast<int>(vy_ * prediction_multiplier);

    // Create points between mouse movement and center of view.
    connect_view_points(points_, vc_, eye::vc, 1);

    // Acquire target.
    target_ = true;
    for (const auto& target : eye_.hulls()) {
      for (const auto& point : points_) {
        if (cv::pointPolygonTest(target, point, false) > 0.0) {
          goto acquired;
        }
      }
      if (cv::norm(centroid(target, spread) - vc_) < spread / 2.0) {
        goto acquired;
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
      if (std::abs(vx_) > 4.0f || std::abs(vy_) > 4.0f) {
        auto vx = -vx_;
        if (std::abs(vx) > 8) {
          vx = vx < 0.0f ? -8.0f : 8.0f;
        }
        auto vy = -vy_;
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
    if constexpr (draw_centroids) {
      for (const auto& target : eye_.hulls()) {
        eye_.draw(overlay, centroid(target, spread), 0x64DD17FF);
      }
    }
    eye_.draw(overlay, vc_, target_ ? 0xD50000FF : 0x00B0FFFF);
    std::format_to(std::back_inserter(info_), "{:05.1f} x | {:05.1f} y", vx_, vy_);
    eye_.draw(overlay, { 2, eye::vh - 40 }, info_);
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
  float vx_{};
  float vy_{};
  cv::Point vc_{};
  bool target_{ false };
  std::vector<cv::Point> points_;
  clock::time_point lockout_{};
  std::string info_;
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
      eye_.draw(overlay, { 2, eye::vh - 40 }, info_);
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