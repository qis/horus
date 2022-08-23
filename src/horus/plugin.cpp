#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <horus/eye.hpp>
#include <horus/log.hpp>
#include <horus/obs.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <atomic>
#include <chrono>
#include <exception>
#include <fstream>
#include <memory>
#include <vector>

#include <Windows.h>
#include <SDL.h>

#define HORUS_LOGGER_LOG "C:/OBS/horus.log"
#define HORUS_CONFIG_TXT "C:/OBS/horus.txt"
#define HORUS_EFFECT_DIR "C:/OBS/horus/res"
#define HORUS_IMAGES_DIR "C:/OBS/img"
#define HORUS_DRAW_SCANS 1
#define HORUS_SHOW_STATS 1
#define HORUS_PLAY_SOUND 1

namespace horus {

class plugin {
public:
  using clock = std::chrono::high_resolution_clock;

  // Measured time between shots: 60 frames @ 75 fps
  static constexpr std::chrono::milliseconds shot_duration{ 800 };

  // Measured time between shot and error (9.0 - 32.0): 21 frames @ 75 fps
  static constexpr std::chrono::milliseconds ammo_duration{ 280 };

  // Measured time between 0 and 12: 104 frames @ 75 fps
  static constexpr std::chrono::milliseconds reset_duration{ 1387 };

  // Measured time between 0 and shot: 171 frames @ 75 fps
  static constexpr std::chrono::milliseconds reload_duration{ 2280 };

  // Time between injected clicks to simulate 8 clicks per second.
  static constexpr std::chrono::milliseconds click_duration{ 125 };

  // Expected frame duration at 75 fps.
  static constexpr std::chrono::milliseconds expected_frame_duration{ 1000 / 75 };

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

    draw_ = gs_effect_create_from_file(HORUS_EFFECT_DIR "/draw.effect", nullptr);
    if (!draw_) {
      log("{:016X}: could not load draw effect: {}", name_, HORUS_EFFECT_DIR "/draw.effect");
    }

    obs_leave_graphics();

    SDL_LoadWAV(HORUS_EFFECT_DIR "/ping.wav", &audio_spec_, &audio_buffer_, &audio_length_);
    if (audio_buffer_) {
      audio_device_ = SDL_OpenAudioDevice(nullptr, 0, &audio_spec_, nullptr, 0);
    }
  }

  plugin(plugin&& other) = delete;
  plugin(const plugin& other) = delete;
  plugin& operator=(plugin&& other) = delete;
  plugin& operator=(const plugin& other) = delete;

  ~plugin()
  {
    if (audio_buffer_) {
      if (audio_device_) {
        SDL_CloseAudioDevice(audio_device_);
      }
      SDL_FreeWAV(audio_buffer_);
    }
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
#if HORUS_SHOW_STATS
    const auto tp0 = clock::now();
#endif
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
      uint8_t* data = nullptr;

      gs_projection_push();
      gs_ortho(
        float(eye::sx),
        float(eye::sx + eye::sw),
        float(eye::sy),
        float(eye::sy + eye::sh),
        -100.0f,
        100.0f);
      obs_source_video_render(target);
      gs_ortho(
        float(eye::ax),
        float(eye::ax + eye::aw),
        float(eye::ay),
        float(eye::ay + eye::ah),
        -100.0f,
        100.0f);
      gs_set_viewport(0, 0, eye::aw, eye::ah);
      obs_source_video_render(target);
      gs_projection_pop();
      gs_texrender_end(texrender_);

      gs_stage_texture(stagesurf_, gs_texrender_get_texture(texrender_));
      if (gs_stagesurface_map(stagesurf_, &data, &line)) {
        // Determine if a target is acquired.
        const auto shoot = eye_.scan(data);

        // Measure the time it takes to decide if a target is acquired.
        tp1 = clock::now();

        // Inject left-click mouse event.
        auto injected = false;
        if (shoot && ready_) {
          const auto rbutton = rbutton_state.load(std::memory_order_acquire);
          const auto xbutton = xbutton_state.load(std::memory_order_acquire);
          if (rbutton != xbutton) {
            // TODO: Inject left-click mouse event.
#if HORUS_PLAY_SOUND
            if (audio_device_) {
              SDL_QueueAudio(audio_device_, audio_buffer_, audio_length_);
              SDL_PauseAudioDevice(audio_device_, 0);
            }
#endif
            injected = true;
          }
        }

        // Update the ready_ value.
        update(tp0, eye_.ammo(data), injected);

        // Handle screenshot request.
        bool screenshot_expected = true;
        if (screenshot_request.compare_exchange_strong(screenshot_expected, false)) {
          screenshot(data, screenshot_counter.fetch_add(1));
          if (audio_device_) {
            SDL_QueueAudio(audio_device_, audio_buffer_, audio_length_);
            SDL_PauseAudioDevice(audio_device_, 0);
          }
        }

#if HORUS_DRAW_SCANS
        overlay = true;
        eye_.draw(data, 0x09BC2460, -1, 0x08DE29C0, -1);
        if (shoot) {
          eye::draw_reticle(data, 0xFFFFFFFF, 0x00A5E7FF);
        }
#  if HORUS_SHOW_STATS
        cv::Mat si(eye::sw, eye::sh, CV_8UC4, data, eye::sw * 4);
        const auto tpos = cv::Point(10, eye::sh - 10);
        auto blocked = 0.0;
        if (tp1 < blocked_) {
          using duration = std::chrono::duration<double>;
          blocked = std::chrono::duration_cast<duration>(blocked_ - tp1).count();
        }
        stats_.clear();
        std::format_to(
          std::back_inserter(stats_),
          "{:02d} fps | {:02.1f} ms | {:02d}/12 | {:1.3f} s",
          static_cast<int>(frames_per_second_),
          average_duration_,
          ammo_,
          blocked);
        cv::putText(si, stats_, tpos, cv::FONT_HERSHEY_PLAIN, 1.5, { 0, 0, 0, 255 }, 4, cv::LINE_AA);
        cv::putText(si, stats_, tpos, cv::FONT_HERSHEY_PLAIN, 1.5, { 0, 165, 231, 255 }, 2, cv::LINE_AA);
#  endif
        gs_texture_set_image(scan_, data, eye::sw * 4, false);
#endif
        gs_stagesurface_unmap(stagesurf_);
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

#if HORUS_SHOW_STATS
    frame_counter_++;
    const auto tp2 = clock::now();
    processing_duration_ += tp1 - tp0;
    if (frame_time_point_ + std::chrono::milliseconds(100) <= tp1) {
      using duration = std::chrono::duration<float, std::milli>;
      const auto frames = static_cast<float>(frame_counter_);
      const auto frames_duration = std::chrono::duration_cast<duration>(tp0 - frame_time_point_);
      average_duration_ = std::chrono::duration_cast<duration>(processing_duration_).count() / frames;
      frames_per_second_ = frames / (frames_duration.count() / 1000.0f);
      processing_duration_ = processing_duration_.zero();
      frame_time_point_ = tp0;
      frame_counter_ = 0;
    }
#endif
  }

  void update(clock::time_point now, const ammo& ammo, bool injected) noexcept
  {
    // If a mouse event was injected, block for at least 125ms (8 clicks per second).
    if (injected) {
      blocked_ = now + click_duration;
    }

    // Update ammo value.
    if (ammo.error < 50) {
      // Error is very low and very likely to be correct.
      if (ammo_ == 1 && ammo.count == 0) {
        // Value decreased from 1 to 0, block until reload is finished.
        blocked_ = now + reload_duration;
        if (ammo.error > 33) {
          blocked_ -= click_duration;
        }
        ammo_ = ammo.count;
      } else if (ammo_ == 0 && ammo.count >= 12) {
        // Value increased from 0 to 12 or higher, block until reload is finished.
        blocked_ = now + reload_duration - reset_duration;
        if (ammo.error > 33) {
          blocked_ -= click_duration;
        }
        ammo_ = ammo.count;
      } else if (ammo_ == ammo.count + 1) {
        // Value decreased by 1, block until the next round is ready.
        blocked_ = now + shot_duration - click_duration;
        ammo_ = ammo.count;
      } else if (ammo_ != ammo.count) {
        // Unexpected value change.
        if (ammo.error < 33) {
          // Error is low enough to assume an animation is in progress.
          if (ammo.count == 0) {
            // First part of the reload animation detected, block until it is likely to be finished.
            blocked_ = now + reload_duration;
          } else if (ammo.count >= 12) {
            // Second part of the reload animation detected, block until it is likely to be finished.
            blocked_ = now + reload_duration - reset_duration;
          } else {
            // Insert round animation detected, block until it is likely to be finished.
            blocked_ = now + ammo_duration / 2;
          }
          ammo_ = ammo.count;
        }
      }
    }

    // Update ready flag.
    ready_ = now > blocked_;
  }

  static void screenshot() noexcept
  {
    screenshot_request.store(true, std::memory_order_release);
  }

  static void screenshot(uint8_t* image, size_t counter) noexcept
  {
    std::unique_ptr<uint8_t[]> data(new uint8_t[eye::sw * eye::sh * 4]);
    std::memcpy(data.get(), image, eye::sw * eye::sh * 4);
    if (auto sp = screenshot_thread_pool) {
      boost::asio::post(*sp, [data = std::move(data), counter]() noexcept {
        try {
          cv::Mat image(eye::sw, eye::sh, CV_8UC4, data.get(), eye::sw * 4);
          cv::cvtColor(image, image, cv::COLOR_RGBA2BGRA);
          cv::imwrite(std::format(HORUS_IMAGES_DIR "/{:09d}.png", counter), image);
          //const auto ammo = image(cv::Rect(0, 0, eye::aw, eye::ah));
          //cv::imwrite(std::format(HORUS_IMAGES_DIR "/{:02d}.png", 12 - counter), ammo);
        }
        catch (const std::exception& e) {
          log("could not save image: {}", counter);
        }
      });
    }
  }

  static void load() noexcept
  {
    SDL_Init(SDL_INIT_AUDIO);
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
    SDL_Quit();
    if (auto sp = screenshot_thread_pool) {
      sp->join();
      screenshot_thread_pool.reset();
    }
    if (auto os = std::ofstream(HORUS_CONFIG_TXT, std::ios::binary)) {
      os << screenshot_counter.load();
    }
    log("plugin unloaded");
  }

  static void rbutton(bool down) noexcept
  {
    rbutton_state.store(down, std::memory_order_release);
  }

  static void xbutton(bool down) noexcept
  {
    xbutton_state.store(down, std::memory_order_release);
  }

private:
  obs_source_t* source_;
  gs_texrender_t* texrender_{ nullptr };
  gs_stagesurf_t* stagesurf_{ nullptr };
  gs_texture_t* scan_{ nullptr };
  gs_effect_t* draw_{ nullptr };

  std::uintptr_t name_{ 0 };
  eye eye_;

  bool ready_ = true;
  unsigned ammo_ = 0;
  clock::time_point blocked_;

  static inline std::atomic_bool rbutton_state = false;
  static inline std::atomic_bool xbutton_state = false;

  static inline std::atomic_bool screenshot_request{ false };
  static inline std::atomic_size_t screenshot_counter{ 0 };
  static inline std::shared_ptr<boost::asio::thread_pool> screenshot_thread_pool;

  Uint32 audio_length_{ 0 };
  Uint8* audio_buffer_{ nullptr };
  SDL_AudioSpec audio_spec_{};
  SDL_AudioDeviceID audio_device_{ 0 };

#if HORUS_SHOW_STATS
  std::string stats_;
  clock::time_point frame_time_point_{ clock::now() };
  std::chrono::nanoseconds processing_duration_{ 0 };
  std::size_t frame_counter_{ 0 };
  float frames_per_second_ = 0.0f;
  float average_duration_ = 0.0f;
#endif
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
  static constexpr auto size = 255;
  static constexpr auto name = std::string_view("Overwatch");
  static auto text = std::string(static_cast<size_t>(size), '\0');
  if (const auto s = GetWindowText(GetForegroundWindow(), text.data(), size); s > 0) {
    if (std::string_view(text.data(), static_cast<std::size_t>(s)) != name) {
      return CallNextHookEx(hook, code, wparam, lparam);
    }
  }
  switch (wparam) {
  case WM_RBUTTONUP:
    horus::plugin::rbutton(false);
    break;
  case WM_RBUTTONDOWN:
    horus::plugin::rbutton(true);
    break;
  case WM_MBUTTONDOWN:
    horus::plugin::screenshot();
    break;
  case WM_XBUTTONUP:
    if (GET_XBUTTON_WPARAM(reinterpret_cast<PMSLLHOOKSTRUCT>(lparam)->mouseData) == XBUTTON2) {
      horus::plugin::xbutton(false);
    }
    break;
  case WM_XBUTTONDOWN:
    if (GET_XBUTTON_WPARAM(reinterpret_cast<PMSLLHOOKSTRUCT>(lparam)->mouseData) == XBUTTON2) {
      horus::plugin::xbutton(true);
    }
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