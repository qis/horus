#include <horus/game.hpp>
#include <horus/hero.hpp>
#include <horus/obs.hpp>
#include <horus/sound.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/thread_pool.hpp>
#include <opencv2/imgcodecs.hpp>
#include <atomic>
#include <format>
#include <mutex>
#include <thread>
#include <cstdio>

namespace horus {

class plugin {
public:
  enum class view {
    hsv = 0,
    scan,
    mask,
    contours,
    groups,
    hulls,
    shapes,
    polygons,
    hero,
    none,
  };

  static constexpr std::chrono::milliseconds interval{ 200 };

  static inline std::atomic<view> draw{ view::none };

  plugin(obs_data_t* settings, obs_source_t* context) noexcept :
    settings_(settings), source_(context)
  {
    obs_enter_graphics();

    texrender_frame_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    assert(texrender_frame_);

    texrender_scan_ = gs_texrender_create(GS_R8, GS_ZS_NONE);
    assert(texrender_scan_);

    texrender_draw_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    assert(texrender_draw_);

    scan_effect_ = gs_effect_create_from_file("C:/OBS/horus/res/scan.effect", nullptr);
    assert(scan_effect_);

    scan_effect_frame_ = gs_effect_get_param_by_name(scan_effect_, "frame");
    assert(scan_effect_frame_);

    scan_effect_technique_ = gs_effect_get_technique(scan_effect_, "Scan");
    assert(scan_effect_technique_);

    scan_stagesurf_ = gs_stagesurface_create(eye::sw, eye::sh, GS_R8);
    assert(scan_stagesurf_);

    draw_effect_ = gs_effect_create_from_file("C:/OBS/horus/res/draw.effect", nullptr);
    assert(draw_effect_);

    draw_effect_frame_ = gs_effect_get_param_by_name(draw_effect_, "frame");
    assert(draw_effect_frame_);

    draw_effect_overlay_ = gs_effect_get_param_by_name(draw_effect_, "overlay");
    assert(draw_effect_overlay_);

    draw_effect_desaturate_ = gs_effect_get_param_by_name(draw_effect_, "desaturate");
    assert(draw_effect_desaturate_);

    draw_effect_hsv_ = gs_effect_get_param_by_name(draw_effect_, "hsv");
    assert(draw_effect_hsv_);

    overlay_texture_ = gs_texture_create(eye::vw, eye::vh, GS_RGBA_UNORM, 1, nullptr, GS_DYNAMIC);
    assert(overlay_texture_);

    screenshot_stagesurf_ = gs_stagesurface_create(eye::sw, eye::sh, GS_RGBA);
    assert(screenshot_stagesurf_);

    obs_leave_graphics();

    boost::asio::co_spawn(focus_context_, game::monitor(focus_), boost::asio::detached);
    focus_thread_ = std::thread([this]() noexcept {
      boost::system::error_code ec;
      focus_context_.run(ec);
    });

    boost::asio::co_spawn(hid_context_, monitor(), boost::asio::detached);
    hid_thread_ = std::thread([this]() noexcept {
      boost::system::error_code ec;
      hid_context_.run(ec);
    });

    const auto w = 4;
    const auto h = 9;
    auto x = eye::vw - w * 2 - 5;
    auto y = eye::vh - h - 3;
    for (auto i = 0; i < 2; i++) {
      eye::polygon bg;
      eye::polygon fg;
      bg.emplace_back(x, y);
      fg.emplace_back(x + 1, y + 1);
      bg.emplace_back(x + w, y);
      fg.emplace_back(x + w - 1, y + 1);
      bg.emplace_back(x + w, y + h);
      fg.emplace_back(x + w - 1, y + h - 1);
      bg.emplace_back(x, y + h);
      fg.emplace_back(x + 1, y + h - 1);
      pause_icon_[0].push_back(bg);
      pause_icon_[1].push_back(fg);
      x += w + 2;
    }
  }

  plugin(plugin&& other) = delete;
  plugin(const plugin& other) = delete;
  plugin& operator=(plugin&& other) = delete;
  plugin& operator=(const plugin& other) = delete;

  ~plugin()
  {
    screenshot_thread_pool_.join();
    if (hid_thread_.joinable()) {
      hid_context_.stop();
      hid_thread_.join();
    }
    if (focus_thread_.joinable()) {
      focus_context_.stop();
      focus_thread_.join();
    }
    obs_enter_graphics();
    gs_stagesurface_destroy(screenshot_stagesurf_);
    gs_texture_destroy(overlay_texture_);
    gs_effect_destroy(draw_effect_);
    gs_stagesurface_destroy(scan_stagesurf_);
    gs_effect_destroy(scan_effect_);
    gs_texrender_destroy(texrender_draw_);
    gs_texrender_destroy(texrender_scan_);
    gs_texrender_destroy(texrender_frame_);
    obs_leave_graphics();
  }

  void render() noexcept
  {
    // Get frame time point.
    const auto tp0 = clock::now();

    // Get target video source (0.5 μs).
    const auto src = obs_filter_get_target(source_);
    if (!src || obs_source_get_width(src) != eye::dw || obs_source_get_height(src) != eye::dh) {
      obs_source_skip_video_filter(source_);
      return;
    }

    // Render target video source to frame texture (15 μs).
    if (!pause_) {
      if (!gs_texrender_begin(texrender_frame_, eye::sw, eye::sh)) {
        obs_source_skip_video_filter(source_);
        return;
      }
      gs_enable_blending(false);
      gs_ortho(
        float{ eye::sx },
        float{ eye::sx + eye::sw },
        float{ eye::sy },
        float{ eye::sy + eye::sh },
        -100.0f,
        100.0f);
      obs_source_video_render(src);
      gs_texrender_end(texrender_frame_);
    }
    const auto frame = gs_texrender_get_texture(texrender_frame_);

    // Render rgba texture to scan texture (5 μs).
    if (!gs_texrender_begin(texrender_scan_, eye::sw, eye::sh)) {
      obs_source_skip_video_filter(source_);
      return;
    }
    gs_enable_blending(false);
    gs_effect_set_texture(scan_effect_frame_, frame);
    gs_ortho(0.0f, float{ eye::sw }, 0.0f, float{ eye::sh }, -100.0f, 100.0f);
    for (size_t i = 0, max = gs_technique_begin(scan_effect_technique_); i < max; i++) {
      if (gs_technique_begin_pass(scan_effect_technique_, i)) {
        gs_draw_sprite(frame, 0, 0, 0);
        gs_technique_end_pass(scan_effect_technique_);
      }
    }
    gs_technique_end(scan_effect_technique_);
    gs_texrender_end(texrender_scan_);

    // Map scan texture (580 μs).
    uint32_t step = 0;
    uint8_t* data = nullptr;
    gs_stage_texture(scan_stagesurf_, gs_texrender_get_texture(texrender_scan_));
    if (!gs_stagesurface_map(scan_stagesurf_, &data, &step)) {
      obs_source_skip_video_filter(source_);
      return;
    }

    // Process scan.
    const auto scan = eye_.scan({ eye::sw, eye::sh, CV_8UC1, data, step });

    // Process hero.
    if (const auto hero = hero_; !hero || !hero->scan(tp0)) {
      if (view_ == view::hulls) {
        eye_.hulls();
      } else if (view_ == view::polygons) {
        eye_.polygons();
      }
      hid_.movement();
    }

    // Get info time point.
    const auto tp1 = clock::now();

    // Unmap scan texture.
    gs_stagesurface_unmap(scan_stagesurf_);

    // Update scan counter.
    if (scan) {
      scan_duration_ += tp1 - tp0;
      scans_++;
    }

    // Clear info.
    info_.clear();

    // Clear overlay.
    overlay_.setTo(cv::Scalar(0, 0, 0, 0));

    // Write timings info.
    std::format_to(
      std::back_inserter(info_),
      "{:03d} fps | {:03.1f} ms",
      frames_per_second_,
      scan_duration_ms_);

    // Write hero info.
    if (const auto hero = hero_) {
      std::format_to(std::back_inserter(info_), " | {}", hero->name());
    }

    // Draw overlay.
    overlay_hsv_ = false;
    overlay_desaturate_ = true;
    switch (view_) {
    case view::hsv:
      info_.append(" | hsv");
      overlay_desaturate_ = false;
      overlay_hsv_ = true;
      break;
    case view::scan: {
      const auto ms = duration_cast<milliseconds<float>>(eye_.draw_scan(overlay_));
      std::format_to(std::back_inserter(info_), " | scan ({:5.3f} ms)", ms.count());
    } break;
    case view::mask: {
      const auto ms = duration_cast<milliseconds<float>>(eye_.draw_mask(overlay_));
      std::format_to(std::back_inserter(info_), " | mask ({:5.3f} ms)", ms.count());
    } break;
    case view::contours: {
      const auto ms = duration_cast<milliseconds<float>>(eye_.draw_contours(overlay_));
      std::format_to(std::back_inserter(info_), " | contours ({:5.3f} ms)", ms.count());
    } break;
    case view::groups: {
      const auto ms = duration_cast<milliseconds<float>>(eye_.draw_groups(overlay_));
      std::format_to(std::back_inserter(info_), " | groups ({:5.3f} ms)", ms.count());
    } break;
    case view::hulls: {
      const auto ms = duration_cast<milliseconds<float>>(eye_.draw_hulls(overlay_));
      std::format_to(std::back_inserter(info_), " | hulls ({:5.3f} ms)", ms.count());
    } break;
    case view::shapes: {
      const auto ms = duration_cast<milliseconds<float>>(eye_.draw_shapes(overlay_));
      std::format_to(std::back_inserter(info_), " | shapes ({:5.3f} ms)", ms.count());
    } break;
    case view::polygons: {
      const auto ms = duration_cast<milliseconds<float>>(eye_.draw_polygons(overlay_));
      std::format_to(std::back_inserter(info_), " | polygons ({:5.3f} ms)", ms.count());
    } break;
    case view::hero:
      info_.append(" | hero");
      if (const auto hero = hero_) {
        overlay_desaturate_ = hero_->draw(overlay_);
      } else {
        overlay_desaturate_ = false;
      }
      break;
    case view::none:
      overlay_desaturate_ = false;
      break;
    }

    // Handle pause toggle requests.
    if (pause_toggle_.exchange(false)) {
      pause_ = !pause_;
      if (pause_ && gs_stagesurface_map(scan_stagesurf_, &data, &step)) {
        cv::imwrite("C:/OBS/images/scan.png", cv::Mat(eye::sw, eye::sh, CV_8UC1, data, step));
        gs_stagesurface_unmap(scan_stagesurf_);
      }
    }

    // Reset texture renderers.
    gs_texrender_reset(texrender_scan_);
    if (!pause_) {
      gs_texrender_reset(texrender_frame_);
    }

    // Draw info text.
    eye_.draw(overlay_, { 2, eye::vh - 20 }, info_);

    // Draw pause icon.
    if (pause_) {
      cv::fillPoly(overlay_, pause_icon_[0], eye::scalar(0x000000A0), cv::LINE_AA);  // Black
      cv::fillPoly(overlay_, pause_icon_[1], eye::scalar(0xC62828FF), cv::LINE_4);   // 800 Red
    }

    // Set overlay texture image.
    gs_texture_set_image(overlay_texture_, overlay_.data, overlay_.step, false);

    // Draw overlay texture.
    if (overlay_hsv_) {
      gs_effect_set_bool(draw_effect_hsv_, true);
    }
    if (overlay_desaturate_) {
      gs_effect_set_bool(draw_effect_desaturate_, true);
    }
    if (obs_source_process_filter_begin(source_, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING)) {
      gs_blend_state_push();
      gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
      if (pause_) {
        gs_effect_set_texture(draw_effect_frame_, frame);
      }
      gs_effect_set_texture(draw_effect_overlay_, overlay_texture_);
      obs_source_process_filter_end(source_, draw_effect_, eye::dw, eye::dh);
      gs_blend_state_pop();
    } else {
      obs_source_skip_video_filter(source_);
    }

    // Handle screenshot requests.
    if (screenshot_.exchange(false)) {
      screenshot();
    }

    // Update view setting.
    if (const auto view = draw.load(std::memory_order_acquire); view != view_) {
      obs_data_set_int(settings_, "view", static_cast<int>(view));
      view_ = view;
    }

    // Update frame counter.
    if (const auto now = clock::now(); now > frames_timeout_) {
      if (scans_) {
        scan_duration_ms_ = duration_cast<milliseconds<float>>(scan_duration_ / scans_).count();
      }
      frames_duration_ = duration_cast<milliseconds<float>>(now - frames_timeout_ + interval);
      frames_per_second_ = std::round(frames_ / (frames_duration_.count() / 1000.0f));
      frames_timeout_ = now + interval;
      scan_duration_ = {};
      frames_ = 0;
      scans_ = 0;
    }
    frames_++;
  }

private:
  boost::asio::awaitable<void> monitor() noexcept
  {
    const auto executor = co_await boost::asio::this_coro::executor;
    timer timer{ executor };
    while (true) {
      timer.expires_from_now(std::chrono::milliseconds(1));
      if (const auto [ec] = co_await timer.async_wait(); ec) {
        co_return;
      }
      if (!hid_.update()) {
        continue;
      }
      if (hid_.pressed(key::f6)) {
        screenshot_.store(true, std::memory_order_release);
      } else if (hid_.pressed(key::f7)) {
        constexpr auto size = static_cast<int>(view::none) + 1;
        const auto data = static_cast<int>(draw.load(std::memory_order_relaxed));
        draw.store(static_cast<view>((data + size - 1) % size), std::memory_order_release);
      } else if (hid_.pressed(key::f8)) {
        constexpr auto size = static_cast<int>(view::none) + 1;
        const auto data = static_cast<int>(draw.load(std::memory_order_relaxed));
        draw.store(static_cast<view>((data + 1) % size), std::memory_order_release);
      } else if (hid_.pressed(key::f9)) {
        hero_ = hero::next_damage_hero(executor, hero_, eye_, hid_);
        announce(hero_->name());
      } else if (hid_.pressed(key::f10)) {
        hero_ = hero::next_support_hero(executor, hero_, eye_, hid_);
        announce(hero_->name());
      } else if (hid_.pressed(key::f11) && hero_) {
        hero_.reset();
        announce("none");
      } else if (hid_.pressed(key::pause)) {
        pause_toggle_.store(true, std::memory_order_release);
      }
      if (const auto hero = hero_) {
        if (focus_.load(std::memory_order_acquire)) {
          co_await hero->update();
        }
      }
    }
    co_return;
  }

  void screenshot() noexcept
  {
    if (!gs_texrender_begin(texrender_draw_, eye::sw, eye::sh)) {
      return;
    }
    gs_enable_blending(false);
    gs_ortho(
      float{ eye::sx },
      float{ eye::sx + eye::sw },
      float{ eye::sy },
      float{ eye::sy + eye::sh },
      -100.0f,
      100.0f);
    obs_source_video_render(source_);
    gs_texrender_end(texrender_draw_);

    uint32_t step = 0;
    uint8_t* data = nullptr;
    gs_stage_texture(screenshot_stagesurf_, gs_texrender_get_texture(texrender_draw_));
    if (!gs_stagesurface_map(screenshot_stagesurf_, &data, &step)) {
      return;
    }
    std::unique_ptr<uchar[]> bytes(new uchar[step * eye::sh]);
    std::memcpy(bytes.get(), data, step * eye::sh);
    gs_stagesurface_unmap(screenshot_stagesurf_);
    gs_texrender_reset(texrender_draw_);

    boost::asio::post(screenshot_thread_pool_, [this, bytes = std::move(bytes), step]() noexcept {
      try {
        const auto index = screenshot_index_.fetch_add(1);
        const auto filename = std::format("C:/OBS/images/{:04d}.png", index);
        cv::Mat image(eye::sw, eye::sh, CV_8UC4, bytes.get(), step);
        cv::cvtColor(image, image, cv::COLOR_RGBA2BGRA);
        cv::imwrite(filename, image);
      }
      catch (...) {
      }
    });
  }

  obs_data_t* settings_;
  obs_source_t* source_;

  gs_texrender_t* texrender_frame_{ nullptr };
  gs_texrender_t* texrender_scan_{ nullptr };
  gs_texrender_t* texrender_draw_{ nullptr };

  gs_effect_t* scan_effect_{ nullptr };
  gs_eparam_t* scan_effect_frame_{ nullptr };
  gs_technique_t* scan_effect_technique_{ nullptr };
  gs_stagesurf_t* scan_stagesurf_{ nullptr };

  gs_effect_t* draw_effect_{ nullptr };
  gs_eparam_t* draw_effect_frame_{ nullptr };
  gs_eparam_t* draw_effect_overlay_{ nullptr };
  gs_eparam_t* draw_effect_desaturate_{ nullptr };
  gs_eparam_t* draw_effect_hsv_{ nullptr };
  bool draw_{ false };

  cv::Mat overlay_{ eye::vw, eye::vh, CV_8UC4 };
  gs_texture_t* overlay_texture_{ nullptr };
  bool overlay_desaturate_{ false };
  bool overlay_hsv_{ false };

  std::shared_ptr<hero::base> hero_;

  std::thread focus_thread_;
  boost::asio::io_context focus_context_{ 1 };
  std::atomic_bool focus_{ false };

  std::thread hid_thread_;
  boost::asio::io_context hid_context_{ 1 };
  hid hid_{ hid_context_.get_executor() };

  eye eye_;

  view view_{ draw.load() };

  std::string info_;

  std::size_t scans_{ 0 };
  std::size_t frames_{ 0 };
  clock::time_point frames_timeout_{ clock::now() + std::chrono::milliseconds{ 100 } };
  horus::milliseconds<float> frames_duration_{};
  int frames_per_second_{ 0 };

  clock::duration scan_duration_{};
  float scan_duration_ms_{ std::numeric_limits<float>::quiet_NaN() };

  bool pause_{ false };
  std::array<std::vector<eye::polygon>, 2> pause_icon_;
  std::atomic_bool pause_toggle_{ false };

  std::atomic_bool screenshot_{ false };
  std::atomic_size_t screenshot_index_{ 0 };
  gs_stagesurf_t* screenshot_stagesurf_{ nullptr };
  boost::asio::thread_pool screenshot_thread_pool_{ 1 };
};

}  // namespace horus

extern "C" {

obs_module_t* module = nullptr;

MODULE_EXPORT const char* obs_module_name()
{
  return "Horus";
}

MODULE_EXPORT const char* obs_module_description()
{
  return "Horus Filter";
}

MODULE_EXPORT uint32_t obs_module_ver()
{
  return LIBOBS_API_VER;
}

MODULE_EXPORT void obs_module_set_pointer(obs_module_t* module)
{
  module = module;
}

static const char* horus_name(void* data)
{
  return "Horus";
}

static obs_properties_t* horus_properties(void* data)
{
  const auto properties = horus::obs_properties_create();
  const auto max = static_cast<int>(horus::plugin::view::none);
  horus::obs_properties_add_int(properties, "view", "View", 0, max, 1);
  return properties;
}

static void horus_defaults(obs_data_t* settings)
{
  const auto max = static_cast<int>(horus::plugin::view::none);
  horus::obs_data_set_default_int(settings, "view", max);
}

static void horus_update(void* data, obs_data_t* settings)
{
  const auto view = horus::obs_data_get_int(settings, "view");
  horus::plugin::draw.store(static_cast<horus::plugin::view>(view));
}

static void* horus_create(obs_data_t* settings, obs_source_t* context)
{
  horus_update(nullptr, settings);
  return new horus::plugin(settings, context);
}

static void horus_destroy(void* data)
{
  delete static_cast<horus::plugin*>(data);
}

static void horus_render(void* data, gs_effect_t* effect)
{
  static_cast<horus::plugin*>(data)->render();
}

static obs_source_info source = {
  .id = "horus_filter",
  .type = OBS_SOURCE_TYPE_FILTER,
  .output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_DO_NOT_DUPLICATE,
  .get_name = horus_name,
  .create = horus_create,
  .destroy = horus_destroy,
  .get_defaults = horus_defaults,
  .get_properties = horus_properties,
  .update = horus_update,
  .video_render = horus_render,
};

MODULE_EXPORT bool obs_module_load()
{
  horus::obs_initialize();
  horus::obs_register_source_s(&source, sizeof(source));
  return true;
}

MODULE_EXPORT void obs_module_unload() {}

}  // extern "C"