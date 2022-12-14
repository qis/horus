#include <horus/hero.hpp>
#include <horus/log.hpp>
#include <horus/obs.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <rock/client.hpp>
#include <atomic>
#include <chrono>
#include <exception>
#include <fstream>
#include <map>
#include <memory>
#include <vector>

#include <Windows.h>

namespace horus {

class plugin {
public:
  using clock = std::chrono::high_resolution_clock;

  static inline std::atomic_size_t screenshot_index{ 0 };
  static inline std::atomic_bool screenshot_request{ false };
  static inline std::shared_ptr<boost::asio::thread_pool> screenshot_thread_pool;

  plugin(obs_source_t* context) noexcept : source_(context)
  {
    name_ = reinterpret_cast<std::uintptr_t>(this);

    log("{:016X}: plugin created", name_);

    obs_enter_graphics();

    stagesurf_ = gs_stagesurface_create(eye::sw, eye::sh, GS_RGBA);
    if (!stagesurf_) {
      log("{:016X}: could not create stage surface", name_);
      return;
    }

    texrender_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    if (!texrender_) {
      log("{:016X}: could not create texture renderer", name_);
      return;
    }

    scan_ = gs_texture_create(eye::sw, eye::sh, GS_RGBA_UNORM, 1, nullptr, GS_DYNAMIC);
    if (!scan_) {
      log("{:016X}: could not create scan texture", name_);
    }

    draw_ = gs_effect_create_from_file(HORUS_RES "/draw.effect", nullptr);
    if (!draw_) {
      log("{:016X}: could not load draw effect: {}", name_, HORUS_RES "/draw.effect");
    }

    obs_leave_graphics();
  }

  plugin(plugin&& other) = delete;
  plugin(const plugin& other) = delete;
  plugin& operator=(plugin&& other) = delete;
  plugin& operator=(const plugin& other) = delete;

  ~plugin()
  {
    if (draw_) {
      gs_effect_destroy(draw_);
    }
    if (scan_) {
      gs_texture_destroy(scan_);
    }
    if (stagesurf_) {
      gs_stagesurface_destroy(stagesurf_);
    }
    if (texrender_) {
      gs_texrender_destroy(texrender_);
    }
    log("{:016X}: plugin destroyed", name_);
  }

  void render() noexcept
  {
    using namespace std::chrono_literals;

    const auto tp0 = clock::now();
    clock::time_point tp1;

    const auto target = obs_filter_get_target(source_);
    if (!target) {
      obs_source_skip_video_filter(source_);
      return;
    }

    const auto cx = obs_source_get_width(target);
    const auto cy = obs_source_get_height(target);
    if (!cx || !cy) {
      obs_source_skip_video_filter(source_);
      return;
    }

    bool overlay = false;
    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
    gs_texrender_reset(texrender_);
    if (gs_texrender_begin(texrender_, eye::sw, eye::sh)) {
      uint32_t line = 0;
      uint8_t* image = nullptr;

      gs_projection_push();

      // Draw scan.
      gs_ortho(
        float(eye::sx),
        float(eye::sx + eye::sw),
        float(eye::sy),
        float(eye::sy + eye::sh),
        -100.0f,
        100.0f);
      obs_source_video_render(target);

      // Draw user interface.
      gs_ortho(
        float(eye::gx + eye::ux),
        float(eye::gx + eye::ux + eye::uw),
        float(eye::gy + eye::uy),
        float(eye::gy + eye::uy + eye::uh),
        -100.0f,
        100.0f);
      gs_set_viewport(0, 0, eye::uw, eye::uh);
      obs_source_video_render(target);

      gs_projection_pop();
      gs_texrender_end(texrender_);

      gs_stage_texture(stagesurf_, gs_texrender_get_texture(texrender_));
      if (gs_stagesurface_map(stagesurf_, &image, &line)) {
        // Get keyboard and mouse state.
        hid_.get(keybd_);
        hid_.get(mouse_);

        // Scan using hero.
        if (hero_) {
          hero_->scan(image, keybd_, mouse_, tp0);
        }

        // Measure scan duration.
        tp1 = clock::now();

        // Load and announce hero.
        const auto hero_key = hero_key_;
        hero_key_ = keybd_.f12;
        if (!hero_key && hero_key_) {
          hero_ = hero::next(hero_, eye_, client_);
        }

        // Draw overlay and information.
        if (DRAW_OVERLAY) {
          eye_.scan(image);
          eye::desaturate(image);

          //eye_.draw_color(image, 0x08DE29C0);
          //eye_.draw_color_mask(image, 0x08DE29C0);
          //eye_.draw_outlines(image, 0x08DE29FF);
          //eye_.draw_regions(image, 0x08DE29C0);
          //eye_.draw_contours(image, 0x08DE29C0);
          //eye_.draw_polygons(image, 0x08DE2990);
          //eye_.draw_hulls(image, 0x08DE2990);
          //eye_.draw_shapes(image, 0x08DE2990);
          //eye_.draw_groups(image, 0x08DE2990);
          eye_.draw_targets(image, 0x08DE2990);
          eye_.draw_contours(image, 0x08DE29C0);
          eye_.draw_points(image, 0xFFFFFF88);

          eye_.draw_stats(image, 0x09BC2460);

          stats_.clear();
          std::format_to(
            std::back_inserter(stats_),
            "{:03d} fps | {:03.1f} ms",
            static_cast<int>(frames_per_second_),
            average_duration_);

          const auto tpos = cv::Point(10, eye::sh - 10);
          cv::Mat si(eye::sw, eye::sh, CV_8UC4, image, eye::sw * 4);
          cv::putText(si, stats_, tpos, cv::FONT_HERSHEY_PLAIN, 1.5, { 0, 0, 0, 255 }, 4, cv::LINE_AA);
          cv::putText(si, stats_, tpos, cv::FONT_HERSHEY_PLAIN, 1.5, { 0, 165, 231, 255 }, 2, cv::LINE_AA);

          // Release image.
          gs_texture_set_image(scan_, image, eye::sw * 4, false);
          gs_stagesurface_unmap(stagesurf_);
          overlay = true;
        }

        // Handle screenshot request.
        bool screenshot_expected = true;
        if (screenshot_request.compare_exchange_weak(screenshot_expected, false)) {
          screenshot(image);
        }
      }
    }
    gs_blend_state_pop();

    if (overlay && draw_) {
      if (obs_source_process_filter_begin(source_, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING)) {
        gs_blend_state_push();
        gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
        gs_effect_set_texture_srgb(gs_effect_get_param_by_name(draw_, "scan"), scan_);
        obs_source_process_filter_end(source_, draw_, 0, 0);
        gs_blend_state_pop();
      }
    } else {
      obs_source_skip_video_filter(source_);
    }

    if (DRAW_OVERLAY) {
      frame_counter_++;
      const auto tp2 = clock::now();
      processing_duration_ += tp1 - tp0;
      if (frame_time_point_ + std::chrono::milliseconds(100) <= tp1) {
        using duration = std::chrono::duration<float, std::milli>;
        const auto frames = static_cast<float>(frame_counter_);
        const auto frames_duration = std::chrono::duration_cast<duration>(tp0 - frame_time_point_);
        const auto processing_duration = std::chrono::duration_cast<duration>(processing_duration_);
        average_duration_ = processing_duration.count() / frames;
        frames_per_second_ = frames / (frames_duration.count() / 1000.0f);
        processing_duration_ = processing_duration_.zero();
        frame_time_point_ = tp0;
        frame_counter_ = 0;
      }
    }
  }

  void screenshot(uint8_t* image) noexcept
  {
    std::unique_ptr<uint8_t[]> data(new uint8_t[eye::sw * eye::sh * 4]);
    std::memcpy(data.get(), image, eye::sw * eye::sh * 4);
    if (auto sp = screenshot_thread_pool) {
      boost::asio::post(*sp, [this, data = std::move(data)]() noexcept {
        try {
          const auto index = screenshot_index.fetch_add(1);
          const auto filename = std::format(HORUS_RES "/screenshot{:04d}.png", index);
          cv::Mat image(eye::sw, eye::sh, CV_8UC4, data.get(), eye::sw * 4);
          cv::cvtColor(image, image, cv::COLOR_RGBA2BGRA);
          cv::imwrite(filename, image);
        }
        catch (const std::exception& e) {
          log("could not create screenshot: {}", e.what());
        }
      });
    }
  }

  static void load() noexcept
  {
    SDL_Init(SDL_INIT_AUDIO);
    screenshot_thread_pool = std::make_shared<boost::asio::thread_pool>(1);
    log("plugin loaded");
  }

  static void unload() noexcept
  {
    SDL_Quit();
    if (auto sp = screenshot_thread_pool) {
      sp->join();
      screenshot_thread_pool.reset();
    }
    log("plugin unloaded");
  }

private:
  obs_source_t* source_;
  gs_texrender_t* texrender_{ nullptr };
  gs_stagesurf_t* stagesurf_{ nullptr };
  gs_texture_t* scan_{ nullptr };
  gs_effect_t* draw_{ nullptr };

  std::uintptr_t name_{ 0 };

  eye eye_;
  hid hid_;
  hid::keybd keybd_;
  hid::mouse mouse_;
  rock::client client_;
  std::unique_ptr<hero::base> hero_;
  bool hero_key_{ false };

  std::string stats_;
  clock::time_point frame_time_point_{ clock::now() };
  std::chrono::nanoseconds processing_duration_{ 0 };
  std::size_t frame_counter_{ 0 };
  float frames_per_second_{ 0.0f };
  float average_duration_{ 0.0f };
};

}  // namespace horus

extern "C" {

obs_module_t* module = nullptr;
std::shared_ptr<horus::logger> logger;

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

static void* horus_create(obs_data_t* settings, obs_source_t* context)
{
  return new horus::plugin(context);
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
  .output_flags = OBS_SOURCE_VIDEO,
  .get_name = horus_name,
  .create = horus_create,
  .destroy = horus_destroy,
  .video_render = horus_render,
};

static HHOOK hook = nullptr;

static LRESULT CALLBACK HookProc(int code, WPARAM wparam, LPARAM lparam)
{
  if (wparam == WM_KEYDOWN && reinterpret_cast<LPKBDLLHOOKSTRUCT>(lparam)->vkCode == VK_F11) {
    horus::plugin::screenshot_request.store(true, std::memory_order_release);
  }
  return CallNextHookEx(hook, code, wparam, lparam);
}

MODULE_EXPORT bool obs_module_load()
{
  logger = std::make_shared<horus::logger>(HORUS_LOG);
  if (!horus::initialize()) {
    return false;
  }
  horus::obs_register_source_s(&source, sizeof(source));
  hook = SetWindowsHookEx(WH_KEYBOARD_LL, HookProc, nullptr, 0);
  horus::plugin::load();
  return true;
}

MODULE_EXPORT void obs_module_unload()
{
  if (hook) {
    UnhookWindowsHookEx(hook);
  }
  horus::plugin::unload();
}

}  // extern "C"