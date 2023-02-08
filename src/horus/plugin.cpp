#include <horus/hero.hpp>
#include <horus/obs.hpp>
#include <horus/sound.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/thread_pool.hpp>
#include <opencv2/imgcodecs.hpp>
#include <atomic>
#include <filesystem>
#include <format>
#include <thread>
#include <cstdio>

// OBS Settings > Output > Recording
// Output Mode: Advanced
// Type: Standard
// Recording Format: mkv
// Audio Track: [x] 1
// Encoder: NVIDIA NVENC H.264
// Rescale Output: [ ]
// Rate Control: VBR
// Bitrate: 16000 Kbps
// Max Bitrate: 48000 Kbps
// Keyframe Interval (seconds, 0=auto): 0
// Preset: Quality
// Profile: high
// [ ] Look-ahead
// [x] Psycho Visual Tuning
// GPU: 0
// Max B-frames: 2

namespace horus {

class plugin {
public:
  enum class view {
    mask = 0,
    targets,
    hero,
    none,
  };

  static inline std::atomic<view> draw{ view::none };

  plugin(obs_data_t* settings, obs_source_t* context) noexcept :
    settings_(settings), source_(context)
  {
    obs_enter_graphics();

    texrender_gray_ = gs_texrender_create(GS_R8, GS_ZS_NONE);
    assert(texrender_gray_);

    texrender_rgba_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    assert(texrender_rgba_);

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

    draw_effect_overlay_ = gs_effect_get_param_by_name(draw_effect_, "overlay");
    assert(draw_effect_overlay_);

    draw_effect_desaturate_ = gs_effect_get_param_by_name(draw_effect_, "desaturate");
    assert(draw_effect_desaturate_);

    overlay_texture_ = gs_texture_create(eye::vw, eye::vh, GS_RGBA_UNORM, 1, nullptr, GS_DYNAMIC);
    assert(overlay_texture_);

    screenshot_stagesurf_ = gs_stagesurface_create(eye::sw, eye::sh, GS_RGBA);
    assert(screenshot_stagesurf_);

    obs_leave_graphics();

    boost::asio::co_spawn(context_, monitor(), boost::asio::detached);
    thread_ = std::thread([this]() noexcept {
      boost::system::error_code ec;
      context_.run(ec);
    });
  }

  plugin(plugin&& other) = delete;
  plugin(const plugin& other) = delete;
  plugin& operator=(plugin&& other) = delete;
  plugin& operator=(const plugin& other) = delete;

  ~plugin()
  {
    screenshot_thread_pool_.join();
    if (thread_.joinable()) {
      context_.stop();
      thread_.join();
    }
    obs_enter_graphics();
    gs_stagesurface_destroy(screenshot_stagesurf_);
    gs_texture_destroy(overlay_texture_);
    gs_effect_destroy(draw_effect_);
    gs_stagesurface_destroy(scan_stagesurf_);
    gs_effect_destroy(scan_effect_);
    gs_texrender_destroy(texrender_rgba_);
    gs_texrender_destroy(texrender_gray_);
    obs_leave_graphics();
  }

  void render() noexcept
  {
    using namespace std::chrono_literals;

    // Get frame time point.
    const auto tp0 = clock::now();

    // Get target video source (0.5 μs).
    const auto src = obs_filter_get_target(source_);
    if (!src || obs_source_get_width(src) != eye::dw || obs_source_get_height(src) != eye::dh) {
      obs_source_skip_video_filter(source_);
      return;
    }

    // Render target video source to frame texture (15 μs).
    gs_texrender_reset(texrender_rgba_);
    if (!gs_texrender_begin(texrender_rgba_, eye::sw, eye::sh)) {
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
    gs_texrender_end(texrender_rgba_);
    const auto frame = gs_texrender_get_texture(texrender_rgba_);

    // Render rgba texture to scan texture (5 μs).
    gs_texrender_reset(texrender_gray_);
    if (!gs_texrender_begin(texrender_gray_, eye::sw, eye::sh)) {
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
    gs_texrender_end(texrender_gray_);

    // Map scan texture (580 μs).
    uint32_t step = 0;
    uint8_t* data = nullptr;
    gs_stage_texture(scan_stagesurf_, gs_texrender_get_texture(texrender_gray_));
    if (!gs_stagesurface_map(scan_stagesurf_, &data, &step)) {
      obs_source_skip_video_filter(source_);
      return;
    }

    // Process scan.
    eye_.scan({ eye::sw, eye::sh, CV_8UC1, data, step });
    if (const auto hero = hero_) {
      hero->scan(mx_.exchange(0), my_.exchange(0));
    }

    // Get info time point.
    const auto tp1 = clock::now();

    // Unmap scan texture.
    gs_stagesurface_unmap(scan_stagesurf_);

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
    overlay_desaturate_ = false;
    const auto view_setting = draw.load(std::memory_order_acquire);
    switch (view_setting) {
    case view::mask:
      info_.append(" | mask");
      view_duration_ += eye_.draw_mask(overlay_);
      break;
    case view::targets:
      info_.append(" | targets");
      view_duration_ += eye_.draw_targets(overlay_);
      break;
    case view::hero:
      info_.append(" | hero");
      if (const auto hero = hero_) {
        overlay_desaturate_ = hero_->draw(overlay_);
      }
      break;
    case view::none:
      break;
    }
    if (view_setting != view::none && view_setting != view::hero) {
      std::format_to(std::back_inserter(info_), " ({:5.3f} ms)", view_duration_ms_);
      overlay_desaturate_ = true;
    }

    // Draw info text.
    eye_.draw(overlay_, { 2, eye::vh - 20 }, info_);

    // Set overlay texture image.
    gs_texture_set_image(overlay_texture_, overlay_.data, overlay_.step, false);

    // Draw overlay texture.
    if (overlay_desaturate_) {
      gs_effect_set_bool(draw_effect_desaturate_, true);
    }
    if (obs_source_process_filter_begin(source_, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING)) {
      gs_blend_state_push();
      gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
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
    if (view_ != view_setting) {
      obs_data_set_int(settings_, "view", static_cast<int>(view_setting));
      view_ = view_setting;
    }

    // Update frame counter.
    scan_duration_ += tp1 - tp0;
    const auto now = clock::now();
    if (now > frames_timeout_) {
      constexpr auto interval = 200ms;
      scan_duration_ms_ = duration_cast<milliseconds<float>>(scan_duration_).count() / frames_;
      view_duration_ms_ = duration_cast<milliseconds<float>>(view_duration_).count() / frames_;
      frames_duration_ = duration_cast<milliseconds<float>>(now - frames_timeout_ + interval);
      frames_per_second_ = std::round(frames_ / (frames_duration_.count() / 1000.0f));
      frames_timeout_ = now + interval;
      scan_duration_ = {};
      view_duration_ = {};
      frames_ = 0;
    }
    frames_++;
  }

private:
  boost::asio::awaitable<void> monitor() noexcept
  {
    auto executor = co_await boost::asio::this_coro::executor;
    while (true) {
      timer_.expires_from_now(std::chrono::milliseconds(1));
      if (const auto [ec] = co_await timer_.async_wait(); ec) {
        co_return;
      }
      hid_.update();
      mx_.fetch_add(hid_.mx());
      my_.fetch_add(hid_.my());
      if (hid_.pressed(key::f9)) {
        hero_ = hero::next_damage_hero(executor, hero_, eye_, hid_);
        announce(hero_->name());
      } else if (hid_.pressed(key::f10)) {
        hero_ = hero::next_support_hero(executor, hero_, eye_, hid_);
        announce(hero_->name());
      } else if (hid_.pressed(key::f11) && hero_) {
        hero_.reset();
        announce("none");
      } else if (hid_.pressed(key::f12)) {
        draw.store(
          static_cast<view>(
            (static_cast<int>(draw.load(std::memory_order_relaxed)) + 1) %
            (static_cast<int>(view::none) + 1)),
          std::memory_order_release);
      } else if (hid_.pressed(key::pause)) {
        screenshot_.store(true, std::memory_order_release);
      }
      if (const auto hero = hero_) {
        co_await hero->update();
      } else {
        mx_.store(0, std::memory_order_relaxed);
        my_.store(0, std::memory_order_relaxed);
      }
    }
    co_return;
  }

  void screenshot() noexcept
  {
    gs_texrender_reset(texrender_rgba_);
    if (!gs_texrender_begin(texrender_rgba_, eye::sw, eye::sh)) {
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
    gs_texrender_end(texrender_rgba_);

    uint32_t step = 0;
    uint8_t* data = nullptr;
    gs_stage_texture(screenshot_stagesurf_, gs_texrender_get_texture(texrender_rgba_));
    if (!gs_stagesurface_map(screenshot_stagesurf_, &data, &step)) {
      return;
    }
    std::unique_ptr<uchar[]> bytes(new uchar[step * eye::sh]);
    std::memcpy(bytes.get(), data, step * eye::sh);
    gs_stagesurface_unmap(screenshot_stagesurf_);

    boost::asio::post(screenshot_thread_pool_, [this, bytes = std::move(bytes), step]() noexcept {
      try {
        const auto index = screenshot_index_.fetch_add(1);
        const auto filename = std::format("C:/OBS/screenshots/{:04d}.png", index);
        cv::Mat image(eye::sw, eye::sh, CV_8UC4, bytes.get(), step);
        cv::cvtColor(image, image, cv::COLOR_RGBA2BGRA);
        cv::imwrite(filename, image);
      }
      catch (...) {
      }
    });
  }

  void announce(const char* name) noexcept
  {
    const auto filename = std::format("C:/OBS/horus/res/sounds/hero/{}.wav", name);
    if (std::filesystem::is_regular_file(filename)) {
      hero_sound_ = { filename.data() };
      hero_sound_.play();
    }
  }

  obs_data_t* settings_;
  obs_source_t* source_;

  gs_texrender_t* texrender_gray_{ nullptr };
  gs_texrender_t* texrender_rgba_{ nullptr };

  gs_effect_t* scan_effect_{ nullptr };
  gs_eparam_t* scan_effect_frame_{ nullptr };
  gs_technique_t* scan_effect_technique_{ nullptr };
  gs_stagesurf_t* scan_stagesurf_{ nullptr };

  gs_effect_t* draw_effect_{ nullptr };
  gs_eparam_t* draw_effect_overlay_{ nullptr };
  gs_eparam_t* draw_effect_desaturate_{ nullptr };

  cv::Mat overlay_{ eye::vw, eye::vh, CV_8UC4 };
  gs_texture_t* overlay_texture_{ nullptr };
  bool overlay_desaturate_{ false };

  std::shared_ptr<hero::base> hero_;
  sound hero_sound_;

  std::thread thread_;
  boost::asio::io_context context_{ 1 };
  hero::timer timer_{ context_ };

  std::atomic_int mx_;
  std::atomic_int my_;
  view view_{ draw.load() };
  hid hid_{ context_.get_executor() };
  eye eye_;

  std::string info_;

  std::size_t frames_{ 0 };
  clock::time_point frames_timeout_{ clock::now() + std::chrono::milliseconds{ 100 } };
  horus::milliseconds<float> frames_duration_{};
  int frames_per_second_{ 0 };

  clock::duration scan_duration_{};
  float scan_duration_ms_{};

  clock::duration view_duration_{};
  float view_duration_ms_{};

  std::atomic_bool screenshot_{ false };
  std::atomic_size_t screenshot_index_{ 0 };
  gs_stagesurf_t* screenshot_stagesurf_{ nullptr };
  boost::asio::thread_pool screenshot_thread_pool_{ 1 };
};

boost::asio::awaitable<void> monitor() noexcept
{
  auto executor = co_await boost::asio::this_coro::executor;
  hero::timer timer{ executor };
  hid hid{ executor };
  eye eye;

  std::shared_ptr<hero::base> hero;
  while (true) {
    timer.expires_from_now(std::chrono::milliseconds(1));
    if (const auto [ec] = co_await timer.async_wait(); ec) {
      co_return;
    }
    hid.update();
    if (hid.pressed(key::f9)) {
      hero = hero::next_damage_hero(executor, hero, eye, hid);
      std::puts(hero->name());
    } else if (hid.pressed(key::f10)) {
      hero = hero::next_support_hero(executor, hero, eye, hid);
      std::puts(hero->name());
    }
    if (hero) {
      co_await hero->update();
    }
  }
  co_return;
}

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

MODULE_EXPORT int horus_main(int argc, char* argv[])
{
  int success = EXIT_SUCCESS;
  try {
    boost::asio::io_context context{ 1 };
    boost::asio::signal_set signals{ context, SIGINT, SIGTERM };
    signals.async_wait([&](boost::system::error_code ec, int count) noexcept {
      context.stop();
    });
    boost::asio::co_spawn(context, horus::monitor(), boost::asio::detached);
    context.run();
  }
  catch (const std::exception& e) {
    std::fputs(e.what(), stderr);
    std::fputs("\r\n", stderr);
    std::fflush(stderr);
    success = EXIT_FAILURE;
  }
  return success;
}

}  // extern "C"