#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <horus/log.hpp>
#include <horus/obs.hpp>
#include <horus/scan.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <atomic>
#include <chrono>
#include <exception>
#include <fstream>
#include <memory>
#include <vector>

#include <windows.h>

#define HORUS_LOGGER_LOG "C:/OBS/horus.log"
#define HORUS_CONFIG_TXT "C:/OBS/horus.txt"
#define HORUS_EFFECT_DIR "C:/OBS/horus/res"
#define HORUS_IMAGES_DIR "C:/OBS/img"
#define HORUS_SHOW_STATS 0
#define HORUS_DRAW_SCANS 1

namespace horus {

class plugin {
public:
  using clock = std::chrono::high_resolution_clock;

  plugin(obs_source_t* context) noexcept : source_(context)
  {
    name_ = reinterpret_cast<std::uintptr_t>(this);
    scan_.resize(scan::sw * scan::sh * 4, 0x00);
    overlay_.resize(scan::sw * scan::sh);
    hierarchy_.reserve(1024);
    contours_.reserve(1024);
    polygons_.reserve(1024);

    log("{:016X}: plugin created", name_);

    obs_enter_graphics();

    stagesurf_ = gs_stagesurface_create(scan::sw, scan::sh, GS_RGBA);
    if (!stagesurf_) {
      log("{:016X}: could not create stage surface", name_);
      return;
    }

    texrender_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    if (!texrender_) {
      log("{:016X}: could not create texture renderer", name_);
      return;
    }

    texture_ = gs_texture_create(scan::sw, scan::sh, GS_RGBA_UNORM, 1, nullptr, GS_DYNAMIC);
    if (!texture_) {
      log("{:016X}: could not create texture", name_);
    }

    effect_ = gs_effect_create_from_file(HORUS_EFFECT_DIR "/horus.effect", nullptr);
    if (!effect_) {
      log("{:016X}: could not load effect: {}", name_, HORUS_EFFECT_DIR "/horus.effect");
    }

    obs_leave_graphics();
  }

  plugin(plugin&& other) = delete;
  plugin(const plugin& other) = delete;
  plugin& operator=(plugin&& other) = delete;
  plugin& operator=(const plugin& other) = delete;

  ~plugin()
  {
    if (effect_) {
      gs_effect_destroy(effect_);
    }
    if (texture_) {
      gs_texture_destroy(texture_);
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
#if HORUS_SHOW_STATS
    const auto tp0 = clock::now();
#endif

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
    if (gs_texrender_begin(texrender_, scan::sw, scan::sh)) {
      gs_projection_push();
      gs_ortho(
        float(scan::sx),
        float(scan::sx + scan::sw),
        float(scan::sy),
        float(scan::sy + scan::sh),
        -100.0f,
        100.0f);
      obs_source_video_render(target);
      gs_projection_pop();
      gs_texrender_end(texrender_);

      uint32_t line = 0;
      uint8_t* data = nullptr;
      gs_stage_texture(stagesurf_, gs_texrender_get_texture(texrender_));
      if (gs_stagesurface_map(stagesurf_, &data, &line)) {
        // Take first screenshot.
        size_t screenshot_index = 0;
        bool screenshot_expected = true;
        if (screenshot_request.compare_exchange_strong(screenshot_expected, false)) {
          screenshot_index = screenshot_counter.fetch_add(2);
          screenshot(data, screenshot_index++);
        }

        // Apply filters.
        scan::filter(data, scan_.data());

        // Find contours and polygons.
        const auto shoot = scan::find(scan_.data(), hierarchy_, contours_, polygons_) > 4.0;

#if HORUS_DRAW_SCANS
        // Draw overlay.
        scan::draw(contours_, polygons_, overlay_.data(), data, 0.3f, shoot, false);
        gs_texture_set_image(texture_, data, scan::sw * 4, false);
        overlay = true;
#endif

        // Take second screenshot.
        if (screenshot_index) {
          screenshot(data, screenshot_index);
        }

        gs_stagesurface_unmap(stagesurf_);
      }
    }
    gs_blend_state_pop();

    if (overlay && effect_) {
      if (obs_source_process_filter_begin(source_, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING)) {
        gs_blend_state_push();
        gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
        gs_effect_set_texture_srgb(gs_effect_get_param_by_name(effect_, "target"), texture_);
        obs_source_process_filter_end(source_, effect_, 0, 0);
        gs_blend_state_pop();
      }
    } else {
      obs_source_skip_video_filter(source_);
    }

#if HORUS_SHOW_STATS
    frame_counter_++;
    const auto tp1 = clock::now();
    processing_duration_ += tp1 - tp0;
    if (frame_time_point_ + std::chrono::seconds(1) <= tp1) {
      using duration = std::chrono::duration<float, std::milli>;
      const auto frames = static_cast<float>(frame_counter_);
      const auto frames_duration = std::chrono::duration_cast<duration>(tp1 - frame_time_point_);
      const auto processing_duration = std::chrono::duration_cast<duration>(processing_duration_);
      const auto fps = frames / (frames_duration.count() / 1000.0f);
      log("{:016X}: {:.1f} fps, {:.1f} ms", name_, fps, processing_duration.count() / frames);
      processing_duration_ = processing_duration_.zero();
      frame_time_point_ = tp1;
      frame_counter_ = 0;
    }
#endif
  }

  static void load() noexcept
  {
    if (auto is = std::ifstream(HORUS_CONFIG_TXT, std::ios::binary)) {
      size_t counter = 0;
      is >> counter;
      if (counter > 0) {
        size_t expected = 0;
        screenshot_counter.compare_exchange_strong(expected, counter);
      }
    }
    screenshot_thread_pool = std::make_shared<boost::asio::thread_pool>(1);
    log("plugin loaded");
  }

  static void unload() noexcept
  {
    if (auto sp = screenshot_thread_pool) {
      sp->join();
      screenshot_thread_pool.reset();
    }
    if (auto os = std::ofstream(HORUS_CONFIG_TXT, std::ios::binary)) {
      os << screenshot_counter.load();
    }
    log("plugin unloaded");
  }

  static void shoot(bool rbutton) noexcept
  {
    if (rbutton) {
      screenshot();
    }
  }

  static void screenshot() noexcept
  {
    screenshot_request.store(true, std::memory_order_release);
  }

  static void screenshot(uint8_t* data, size_t counter) noexcept
  {
    std::unique_ptr<uint8_t[]> rgba(new uint8_t[scan::sw * scan::sh * 4]);
    std::memcpy(rgba.get(), data, scan::sw * scan::sh * 4);
    if (auto sp = screenshot_thread_pool) {
      boost::asio::post(*sp, [rgba = std::move(rgba), counter]() noexcept {
        try {
          cv::Mat image(scan::sw, scan::sh, CV_8UC4, rgba.get(), 1024 * 4);
          cv::cvtColor(image, image, cv::COLOR_RGBA2BGRA);
          cv::imwrite(std::format(HORUS_IMAGES_DIR "/{:09d}.png", counter), image);
        }
        catch (const std::exception& e) {
          log("could not save image: {}", counter);
        }
      });
    }
  }

private:
  obs_source_t* source_;
  gs_texrender_t* texrender_{ nullptr };
  gs_stagesurf_t* stagesurf_{ nullptr };
  gs_texture_t* texture_{ nullptr };
  gs_effect_t* effect_{ nullptr };

  std::uintptr_t name_{ 0 };

  std::vector<uint8_t> scan_;
  std::vector<uint8_t> overlay_;
  std::vector<cv::Vec4i> hierarchy_;
  std::vector<std::vector<cv::Point>> contours_;
  std::vector<std::vector<cv::Point>> polygons_;

#if HORUS_SHOW_STATS
  clock::time_point frame_time_point_{ clock::now() };
  std::chrono::nanoseconds processing_duration_{ 0 };
  std::size_t frame_counter_{ 0 };
#endif

  static inline std::atomic_bool screenshot_request{ false };
  static inline std::atomic_size_t screenshot_counter{ 0 };
  static inline std::shared_ptr<boost::asio::thread_pool> screenshot_thread_pool;
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
static bool rbutton = false;

static LRESULT CALLBACK HookProc(int code, WPARAM wparam, LPARAM lparam)
{
  static constexpr int size = 255;
  static std::string text(static_cast<size_t>(size), '\0');
  switch (wparam) {
  case WM_RBUTTONUP:
    rbutton = false;
    break;
  case WM_RBUTTONDOWN:
    rbutton = true;
    break;
  case WM_LBUTTONDOWN:
    if (auto s = GetWindowText(GetForegroundWindow(), text.data(), size); s > 0) {
      if (std::string_view(text.data(), static_cast<std::size_t>(s)) == "Overwatch") {
        horus::plugin::shoot(rbutton);
      }
    }
    break;
  case WM_XBUTTONDOWN:
    horus::plugin::screenshot();
    break;
  }
  return CallNextHookEx(hook, code, wparam, lparam);
}

MODULE_EXPORT bool obs_module_load()
{
  logger = std::make_shared<horus::logger>(HORUS_LOGGER_LOG);
  if (!horus::initialize()) {
    return false;
  }
  horus::obs_register_source_s(&source, sizeof(source));
  hook = SetWindowsHookEx(WH_MOUSE_LL, HookProc, nullptr, 0);
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