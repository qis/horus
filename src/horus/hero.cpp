#include "hero.hpp"
#include <boost/circular_buffer.hpp>
#include <algorithm>
#include <format>
#include <map>
#include <numeric>
#include <optional>

namespace horus::hero {
namespace {

constexpr auto mf = 0.0442846;

__forceinline std::pair<double, double> mouse2view(double mx, double my) noexcept
{
  return { mx * mf, my * mf };
}

constexpr std::pair<std::int16_t, std::int16_t> view2mouse(double vx, double vy) noexcept
{
  return { static_cast<std::int16_t>(vx / mf), static_cast<std::int16_t>(vy / mf) };
}

__forceinline cv::Point center(const cv::Rect& rect) noexcept
{
  return { rect.x + rect.width / 2, rect.y + rect.height / 2 };
}

__forceinline cv::Point centroid(const eye::polygon& polygon) noexcept
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

class mouse_prediction {
public:
  mouse_prediction(std::size_t size) : va_(size) {}

  mouse_prediction(mouse_prediction&& other) = default;
  mouse_prediction(const mouse_prediction& other) = default;
  mouse_prediction& operator=(mouse_prediction&& other) = default;
  mouse_prediction& operator=(const mouse_prediction& other) = default;
  ~mouse_prediction() = default;

  void update(const hid::mouse& mouse, clock::time_point tp, double vm = 4.2) noexcept
  {
    constexpr milliseconds<double> fd{ 1000.0 / eye::fps };
    const auto now = clock::now();
    const auto sc = duration_cast<milliseconds<double>>(now - tp);
    const auto md = duration_cast<milliseconds<double>>(now - mouse.tp);
    const auto mf = md / (fd > sc ? fd - sc : fd);
    std::tie(vx_, vy_) = mouse2view(mouse.mx / mf, mouse.my / mf);
    va_.push_back({ static_cast<int>(vx_ * vm), static_cast<int>(vy_ * vm) });
  }

  constexpr double vx() const noexcept
  {
    return vx_;
  }

  constexpr double vy() const noexcept
  {
    return vy_;
  }

  cv::Point va() const noexcept
  {
    if (va_.empty()) {
      return { 0, 0 };
    }
    const auto size = static_cast<double>(va_.size());
    return std::accumulate(va_.begin(), va_.end(), cv::Point(0, 0)) / size;
  }

private:
  double vx_{ 0.0 };
  double vy_{ 0.0 };
  boost::circular_buffer<cv::Point> va_;
};

}  // namespace

class hitscan : public base {
public:
  hitscan(boost::asio::any_io_executor executor, eye& eye, hid& hid) noexcept :
    base(executor, eye, hid)
  {}

  bool scan(clock::time_point tp, bool focus) noexcept override
  {
    // Handle trigger key.
    trigger_ = down(button::left);
    if (!trigger_) {
      return false;
    }

    // Get targets.
    const auto& targets = eye_.hulls();

    // Update mouse movement.
    mp_.update(movement(), tp);
    vc_ = eye::vc + mp_.va();

    // Acquire target.
    cv::Point vd(0, 0);
    auto mv = eye::vw * 2.0;
    for (const auto& target : targets) {
      if (cv::pointPolygonTest(target, vc_, true) > 0.0) {
        return true;
      }
      const auto cvd = centroid(target) - vc_;
      if (const auto cmv = cv::norm(cvd); cmv < mv) {
        mv = cmv;
        vd = cvd;
      }
    }
    if (std::abs(vd.x) > 16.0 || !focus) {
      return true;
    }

    // Adjust aim.
    const auto vx = std::clamp(vd.x * 0.8, -1.5, 1.5);
    move(view2mouse(vx, 0.0).first, 0);
    return true;
  }

  bool draw(cv::Mat& overlay) noexcept override
  {
    if (trigger_) {
      eye_.draw_hulls(overlay);
      eye_.draw(overlay, vc_, 0xD50000FF);
    }
    return false;
  }

private:
  cv::Point vc_{};
  mouse_prediction mp_{ 3 };
  bool trigger_{ false };
};

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

  bool scan(clock::time_point tp, bool focus) noexcept override
  {
    // Get targets.
    const auto& targets = eye_.polygons();

    // Update mouse movement.
    mp_.update(movement(), tp);
    vc_ = eye::vc + mp_.va();

    // Create points between mouse movement and center of view.
    connect_view_points(points_, vc_, eye::vc, 1);

    // Acquire target.
    target_ = true;
    cv::Point vd(0, 0);
    for (const auto& target : eye_.polygons()) {
      const auto rect = cv::boundingRect(target);
      const auto margin = std::max(1.0, rect.width / 16.0);
      for (const auto& point : points_) {
        if (cv::pointPolygonTest(target, point, true) > margin) {
          vd = vc_ - point;
          goto acquired;
        }
      }
    }
    target_ = false;
  acquired:

    // Set trigger state.
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
    if (now < lockout_ || !trigger_ || !focus) {
      return true;
    }

    // Fire if a target was acquired.
    if (target_) {
      if (cv::norm(vd) > 1.0) {
        const auto md = view2mouse(vd.x, vd.y);
        move(md.first, md.second);
        mask(button::up, 16ms, 16us);
      } else {
        mask(button::up, 16ms);
      }
      lockout_ = now + 128ms;
    }
    return true;
  }

  bool draw(cv::Mat& overlay) noexcept override
  {
    info_.clear();
    eye_.draw_polygons(overlay);
    if (trigger_) {
      eye_.draw(overlay, vc_, target_ ? 0xD50000FF : 0x00B0FFFF);
    }
    std::format_to(std::back_inserter(info_), "{:05.1f} x | {:05.1f} y", mp_.vx(), mp_.vy());
    eye_.draw(overlay, { 2, eye::vh - 40 }, info_);
    return false;
  }

private:
  cv::Point vc_{};
  mouse_prediction mp_{ 3 };
  std::vector<cv::Point> points_;
  clock::time_point lockout_{};
  bool trigger_{ false };
  bool target_{ false };
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

class bastion : public hitscan {
public:
  // Controls
  // ========
  // + RETICLE: CIRCLE
  // + MOVEMENT
  //   JUMP: SPACE | MOUSE BUTTON 5

  bastion(boost::asio::any_io_executor executor, eye& eye, hid& hid) noexcept :
    hitscan(executor, eye, hid)
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

  bool scan(clock::time_point tp, bool focus) noexcept override
  {
    // Get targets.
    const auto& targets = eye_.hulls();

    // Update mouse movement.
    mp_.update(movement(), tp);
    vc_ = eye::vc + mp_.va();

    // Create points between mouse movement and center of view.
    connect_view_points(points_, vc_, eye::vc, 1);

    // Acquire target.
    target_ = true;
    cv::Point vd(0, 0);
    for (const auto& target : targets) {
      for (const auto& point : points_) {
        if (cv::pointPolygonTest(target, point, false) > 0.0) {
          vd = centroid(target) - vc_;
          goto acquired;
        }
      }
      vd = centroid(target) - vc_;
      if (cv::norm(vd) < spread / 2.0) {
        goto acquired;
      }
    }
    target_ = false;
  acquired:

    // Get current time.
    const auto now = clock::now();

    // Set trigger state.
    trigger_ = down(button::right);

    // Handle trigger key.
    if (!trigger_ || !focus) {
      lockout_ = now + 64ms;
      fire_ = false;
      return true;
    }

    // Handle manual fire key.
    if (down(button::left)) {
      lockout_ = now + 64ms;
      fire_ = false;
      return true;
    }

    // Adjust aim.
    if (fire_) {
      const auto vx = std::clamp(vd.x * 0.8, -10.0, 10.0);
      move(view2mouse(vx, 0.0).first, 0);
      mask(button::up, 16ms, 1ms);
      fire_ = false;
    }

    // Handle lockout.
    if (now < lockout_) {
      return true;
    }

    // Adjust crosshair and fire.
    if (target_) {
      const auto vx = std::clamp(vd.x * 0.8, -10.0, 10.0);
      move(view2mouse(vx, 0.0).first, 0);
      lockout_ = now + 500ms;
      fire_ = true;
    }
    return true;
  }

  bool draw(cv::Mat& overlay) noexcept override
  {
    info_.clear();
    eye_.draw_hulls(overlay);
    if constexpr (draw_centroids) {
      for (const auto& target : eye_.hulls()) {
        eye_.draw(overlay, centroid(target), 0x64DD17FF);
      }
    }
    if (trigger_) {
      eye_.draw(overlay, vc_, target_ ? 0xD50000FF : 0x00B0FFFF);
    }
    std::format_to(std::back_inserter(info_), "{:05.1f} x | {:05.1f} y", mp_.vx(), mp_.vy());
    eye_.draw(overlay, { 2, eye::vh - 40 }, info_);
    return false;
  }

private:
  cv::Point vc_{};
  mouse_prediction mp_{ 3 };
  std::vector<cv::Point> points_;
  clock::time_point lockout_{};
  bool trigger_{ false };
  bool target_{ false };
  bool fire_{ false };
  std::string info_;
};

class soldier : public hitscan {
public:
  // Controls
  // ========
  // + MOVEMENT
  //   JUMP: SPACE | MOUSE BUTTON 5
  // + WEAPONS & ABILITIES
  //   ABILITY 1: LSHIFT | MOUSE BUTTON 4

  soldier(boost::asio::any_io_executor executor, eye& eye, hid& hid) noexcept :
    hitscan(executor, eye, hid)
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
    hitscan::draw(overlay);
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