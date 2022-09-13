#include <anubis/client.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <horus/eye.hpp>
#include <horus/log.hpp>
#include <horus/mouse.hpp>
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
    const auto tp0 = clock::now();
    clock::time_point tp1;

    const auto target = obs_filter_get_target(source_);
    if (!target) {
      obs_source_skip_video_filter(source_);
      hero_ = hero::unknown;
      return;
    }

    const auto cx = obs_source_get_width(target);
    const auto cy = obs_source_get_height(target);
    if (!cx || !cy) {
      obs_source_skip_video_filter(source_);
      hero_ = hero::unknown;
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
        float(eye::hx),
        float(eye::hx + eye::hw),
        float(eye::hy),
        float(eye::hy + eye::hh),
        -100.0f,
        100.0f);
      gs_set_viewport(0, 0, eye::hw, eye::hh);
      obs_source_video_render(target);
      gs_projection_pop();
      gs_texrender_end(texrender_);

      gs_stage_texture(stagesurf_, gs_texrender_get_texture(texrender_));
      if (gs_stagesurface_map(stagesurf_, &data, &line)) {
        // Get relative mouse travel distance since last call.
        mouse_.get(mouse_state_);

        // Determine if a target is acquired.
        auto shoot = false;
        if (hero_ == hero::ana || hero_ == hero::ashe || hero_ == hero::reaper) {
          const auto ax = std::pow(std::abs(mouse_state_.mx) * 2.0f, 1.05f);
          const auto ay = std::pow(std::abs(mouse_state_.my) * 2.0f, 1.05f);
          const auto mx = mouse_state_.mx < 0 ? -ax : ax;
          const auto my = mouse_state_.my < 0 ? -ay : ay;
          shoot = eye_.scan(data, mx, my);
        }

#if HORUS_SHOW_STATS
        // Measure the time it takes to decide if a target is acquired.
        tp1 = clock::now();
#endif

        // Press left mouse button.
        auto injected = false;
        if (shoot && ready_ && !mouse_state_.bl && mouse_state_.br != mouse_state_.bu) {
          client_.mask(anubis::button::left, std::chrono::milliseconds(7));
          injected = true;
#if HORUS_PLAY_SOUND
          play_sound();
#endif
        }

        // Update the ready_ value.
        const auto [ hero_class, hero_error ] = eye_.parse(data);
        hero_ = hero_error < 0.5 ? hero_class : hero::unknown;
        update(tp0, mouse_state_.bl || injected);

        // Handle screenshot request.
        bool screenshot_expected = true;
        if (screenshot_request.compare_exchange_strong(screenshot_expected, false)) {
          screenshot(data, screenshot_counter.fetch_add(1));
          play_sound();
        }

#if HORUS_DRAW_SCANS
        if (hero_ == hero::ana || hero_ == hero::ashe || hero_ == hero::reaper) {
          eye_.draw(data, 0x09BC2460, -1, 0x08DE29C0, -1);
          eye_.draw_reticle(data, 0x000000FF, 0x00A5E7FF);
        }
#endif
#if HORUS_SHOW_STATS
        cv::Mat si(eye::sw, eye::sh, CV_8UC4, data, eye::sw * 4);
        const auto tpos = cv::Point(10, eye::sh - 10);
        auto blocked = 0.0;
        if (tp1 < blocked_) {
          using duration = std::chrono::duration<double>;
          blocked = std::chrono::duration_cast<duration>(blocked_ - tp1).count();
        }
        stats_.clear();
        const char* hero_name = "unknown";
        switch (hero_) {
        case hero::ana:
          hero_name = "ana";
          break;
        case hero::ashe:
          hero_name = "ashe";
          break;
        case hero::pharah:
          hero_name = "pharah";
          break;
        case hero::reaper:
          hero_name = "reaper";
          break;
        default:
          break;
        }
        std::format_to(
          std::back_inserter(stats_),
          "{:02d} fps | {:02.1f} ms | {:1.2f} s | {:1.3f} {}",
          static_cast<int>(frames_per_second_),
          average_duration_,
          blocked,
          hero_error,
          hero_name);
        cv::putText(si, stats_, tpos, cv::FONT_HERSHEY_PLAIN, 1.5, { 0, 0, 0, 255 }, 4, cv::LINE_AA);
        cv::putText(si, stats_, tpos, cv::FONT_HERSHEY_PLAIN, 1.5, { 0, 165, 231, 255 }, 2, cv::LINE_AA);
#endif
#if HORUS_DRAW_SCANS || HORUS_SHOW_STATS
        gs_texture_set_image(scan_, data, eye::sw * 4, false);
        overlay = true;
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

  void update(clock::time_point now, bool fire) noexcept
  {
    if (hero_ == hero::pharah || hero_ == hero::unknown) {
      update_pharah(now);
      ready_ = false;
      return;
    }
    if (hero_ != hero::ana && hero_ != hero::ashe && hero_ != hero::reaper) {
      ready_ = false;
      return;
    }
    if (now < blocked_) {
      ready_ = false;
      return;
    }
    if (fire) {
      blocked_ = now + std::chrono::milliseconds(125);  // 8 clicks per second
      ready_ = false;
      return;
    }
    ready_ = true;
  }

  bool pharah_enabled_ = false;
  bool pharah_br_state_ = false;
  clock::time_point pharah_br_state_update_ = clock::now();

  void update_pharah(clock::time_point now) noexcept
  {
    //if (control_state.load(std::memory_order_relaxed)) {
    //  if (pharah_enabled_) {
    //    client_.mask(anubis::button::middle, std::chrono::seconds(0));
    //    pharah_enabled_ = false;
    //  }
    //  return;
    //}

    if (mouse_state_.br) {
      if (!pharah_br_state_) {
        client_.mask(anubis::button::middle, std::chrono::seconds(2));
        pharah_br_state_update_ = now;
        pharah_br_state_ = true;
      } else if (now - pharah_br_state_update_ > std::chrono::seconds(1)) {
        client_.mask(anubis::button::middle, std::chrono::seconds(2));
        pharah_br_state_update_ = now;
      }
    } else {
      if (pharah_br_state_) {
        play_sound();
        client_.mask(anubis::button::middle, std::chrono::seconds(0));
        pharah_br_state_update_ = now;
        pharah_br_state_ = false;
      }
    }
  }

  void play_sound() noexcept
  {
    if (audio_device_) {
      SDL_QueueAudio(audio_device_, audio_buffer_, audio_length_);
      SDL_PauseAudioDevice(audio_device_, 0);
    }
  }

  static void control(bool down) noexcept
  {
    control_state.store(down, std::memory_order_release);
  }

  static void screenshot() noexcept
  {
    screenshot_request.store(true, std::memory_order_release);
  }

  void screenshot(uint8_t* image, size_t counter) noexcept
  {
    std::unique_ptr<uint8_t[]> data(new uint8_t[eye::sw * eye::sh * 4]);
    std::memcpy(data.get(), image, eye::sw * eye::sh * 4);
    if (auto sp = screenshot_thread_pool) {
      boost::asio::post(*sp, [this, data = std::move(data), counter]() noexcept {
        try {
          cv::Mat image(eye::sw, eye::sh, CV_8UC4, data.get(), eye::sw * 4);
          cv::cvtColor(image, image, cv::COLOR_RGBA2BGRA);

          //cv::imwrite(std::format(HORUS_IMAGES_DIR "/{:09d}.png", counter), image);

          //const auto ammo = image(cv::Rect(0, 0, eye::aw, eye::ah));
          //cv::imwrite(std::format(HORUS_IMAGES_DIR "/{:02d}.png", 12 - counter), ammo);

          const auto hero = image(cv::Rect(0, 0, eye::hw, eye::hh));
          cv::imwrite(HORUS_IMAGES_DIR "/hero.png", hero);

          //const auto sr = image(cv::Rect(580, 512, 100, 38));
          //cv::imwrite(std::format(HORUS_IMAGES_DIR "/{:04d}.png", counter), sr);
          //client_.mask(anubis::button::left, std::chrono::milliseconds(7));
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

private:
  obs_source_t* source_;
  gs_texrender_t* texrender_{ nullptr };
  gs_stagesurf_t* stagesurf_{ nullptr };
  gs_texture_t* scan_{ nullptr };
  gs_effect_t* draw_{ nullptr };

  std::uintptr_t name_{ 0 };

  eye eye_;
  mouse mouse_;
  mouse::state mouse_state_;
  anubis::client client_;

  hero hero_ = hero::unknown;
  clock::time_point blocked_;
  bool ready_{ true };

  static inline std::atomic_bool control_state{ false };
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
  float frames_per_second_{ 0.0f };
  float average_duration_{ 0.0f };
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

static HHOOK keyboard_hook = nullptr;

static LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wparam, LPARAM lparam)
{
  static constexpr auto size = 255;
  static constexpr auto name = std::string_view("Overwatch");
  static auto text = std::string(static_cast<size_t>(size), '\0');

  if (const auto ks = reinterpret_cast<LPKBDLLHOOKSTRUCT>(lparam); ks->vkCode == VK_LCONTROL) {
    if (wparam == WM_KEYDOWN || wparam == WM_KEYUP) {
      if (const auto s = GetWindowText(GetForegroundWindow(), text.data(), size); s > 0) {
        if (std::string_view(text.data(), static_cast<std::size_t>(s)) == name) {
          horus::plugin::control(wparam == WM_KEYDOWN);
        }
      }
    }
  }
  return CallNextHookEx(keyboard_hook, code, wparam, lparam);
}

static HHOOK mouse_hook = nullptr;

static LRESULT CALLBACK MouseHookProc(int code, WPARAM wparam, LPARAM lparam)
{
  if (wparam == WM_MBUTTONDOWN) {
    horus::plugin::screenshot();
  }
  return CallNextHookEx(mouse_hook, code, wparam, lparam);
}

MODULE_EXPORT bool obs_module_load()
{
  logger = std::make_shared<horus::logger>(HORUS_LOGGER_LOG);
  if (!horus::initialize()) {
    return false;
  }
  horus::obs_register_source_s(&source, sizeof(source));
  keyboard_hook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProc, nullptr, 0);
#ifndef NDEBUG
  mouse_hook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, nullptr, 0);
#endif
  horus::plugin::load();
  return true;
}

MODULE_EXPORT void obs_module_unload()
{
  if (mouse_hook) {
    UnhookWindowsHookEx(mouse_hook);
  }
  if (keyboard_hook) {
    UnhookWindowsHookEx(keyboard_hook);
  }
  horus::plugin::unload();
}

}  // extern "C"