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
#include <memory>
#include <vector>

#include <Windows.h>
#include <SDL.h>

#define HORUS_LOGGER_LOG "C:/OBS/horus.log"
#define HORUS_CONFIG_TXT "C:/OBS/horus.txt"
#define HORUS_EFFECT_DIR "C:/OBS/horus/res"
#define HORUS_IMAGES_DIR "C:/OBS/img"

namespace horus {

class plugin {
public:
  using clock = std::chrono::high_resolution_clock;

  static constexpr std::chrono::milliseconds pharah_boost_duration{ 1400 };
  static constexpr std::chrono::milliseconds pharah_flight_duration{ 350 };
  static constexpr std::chrono::milliseconds pharah_fall_duration{ 450 };
  static constexpr std::chrono::milliseconds pharah_skip_delta{ 50 };
  static_assert(pharah_skip_delta < pharah_flight_duration);

  static inline std::atomic_bool input_e_state{ false };
  static inline std::atomic_bool input_q_state{ false };
  static inline std::atomic_bool input_space_state{ false };
  static inline std::atomic_bool input_shift_state{ false };
  static inline std::atomic_bool input_control_state{ false };

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
      hero_ = hero::type::none;
      return;
    }

    const auto cx = obs_source_get_width(target);
    const auto cy = obs_source_get_height(target);
    if (!cx || !cy) {
      obs_source_skip_video_filter(source_);
      hero_ = hero::type::none;
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
        // Get mouse state.
        hid_.get(mouse_);

        // Scan image with current hero.
        const auto draw = hitscan_.scan(data, mouse_);

        // Measure scan duration.
        tp1 = clock::now();

        // Determine current hero.
        const auto [type, error] = eye_.type(data);
        //hero_ = error < 0.5 ? type : hero::type::none;

        // Handle screenshot request.
        bool screenshot_expected = true;
        if (screenshot_request.compare_exchange_strong(screenshot_expected, false)) {
          screenshot(data, screenshot_counter.fetch_add(1));
          play_sound();
        }

        // Draw overlay.
        if (draw) {
          eye_.draw(data, 0x09BC2460, -1, 0x08DE29C0, -1);
          eye_.draw_reticle(data, 0x000000FF, 0x00A5E7FF);
        }

        // Draw information.
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
          "{:02d} fps | {:02.1f} ms | {:1.2f} s | {:1.3f} | {}",
          static_cast<int>(frames_per_second_),
          average_duration_,
          blocked,
          error,
          hero::name(type));
        cv::putText(si, stats_, tpos, cv::FONT_HERSHEY_PLAIN, 1.5, { 0, 0, 0, 255 }, 4, cv::LINE_AA);
        cv::putText(si, stats_, tpos, cv::FONT_HERSHEY_PLAIN, 1.5, { 0, 165, 231, 255 }, 2, cv::LINE_AA);

        // Release image.
        gs_texture_set_image(scan_, data, eye::sw * 4, false);
        gs_stagesurface_unmap(stagesurf_);
        overlay = true;
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
  }

  void update(clock::time_point now, bool fire) noexcept
  {
    if (hero_ == hero::type::pharah) {
      pharah_enabled_ = true;
    } else if (hero_ != hero::type::none) {
      pharah_enabled_ = false;
    }
    if (pharah_enabled_) {
      update_pharah(now);
      //input_e_state_ = input_e_state.load(std::memory_order_relaxed);
      //input_q_state_ = input_q_state.load(std::memory_order_relaxed);
      //input_space_state_ = input_space_state.load(std::memory_order_relaxed);
      //input_shift_state_ = input_shift_state.load(std::memory_order_relaxed);
      //input_control_state_ = input_control_state.load(std::memory_order_relaxed);
      //input_rmb_state_ = mouse_state_.br;
      ready_ = false;
      return;
    }
    if (hero_ != hero::type::ana && hero_ != hero::type::ashe && hero_ != hero::type::reaper) {
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

  enum class pharah_state {
    manual,
    jump_jet,
    automatic,
  };
  pharah_state pharah_state_ = pharah_state::manual;

  bool pharah_enabled_ = false;
  clock::time_point pharah_update_ = clock::now();
  clock::time_point pharah_boost_time_point_ = clock::now();
  bool pharah_boost_ = false;
  bool pharah_skip_ = false;


  // There are two modes of flight: automatic and manual.
  // - Manual mode is triggered by holding down SPACE or RMB and overwrites automatic mode.
  // - Automatic mode is triggered by SHIFT,
  //
  // - CTRL or Q reset mask unless SPACE or RMB are pressed.

  // Scenarios
  // 1. When RMB is pressed, press MMB every second.
  // 2.
  // 1. When CTRL or Q is pressed disable automation.
  // 2. SPACE enables automation upon release.

  void update_pharah(clock::time_point now) noexcept
  {
    // input_e_state_ = input_e_state.load(std::memory_order_relaxed);
    // input_q_state_ = input_q_state.load(std::memory_order_relaxed);
    // input_space_state_ = input_space_state.load(std::memory_order_relaxed);
    // input_shift_state_ = input_shift_state.load(std::memory_order_relaxed);
    // input_control_state_ = input_control_state.load(std::memory_order_relaxed);
    // input_rmb_state_ = mouse_state_.br;

    /*
    // TODO: Do not block right click during boost.
    //
    // Handle control and Q keys.
    if (control_state.load(std::memory_order_relaxed) || q_state.load(std::memory_order_relaxed)) {
      // Unset middle mouse button mask when control key is pressed.
      if (pharah_state_ != pharah_state::disabled) {
        client_.mask(rock::button::middle, std::chrono::seconds(0));
        pharah_state_ = pharah_state::disabled;
      }
      return;
    }

    // Handle E key.
    if (e_state.load(std::memory_order_relaxed)) {
      pharah_state_ = pharah_state::flight_button_released;
      pharah_update_ = now - pharah_flight_duration - pharah_fall_duration;
      return;
    }

    // Handle shift key.
    if (shift_state.load(std::memory_order_relaxed)) {
      // Release middle mouse button until pharah_boost_duration time passes.
      if (pharah_state_ != pharah_state::rocket_button_pressed) {
        client_.mask(rock::button::middle, std::chrono::seconds(0));
        pharah_state_ = pharah_state::rocket_button_pressed;
        pharah_boost_time_point_ = now;
        pharah_boost_ = true;
        pharah_update_ = now;
      }
      return;
    }

    // Wait for boost to finish, then press middle mouse button.
    if (pharah_state_ == pharah_state::rocket_button_pressed) {
      if (mouse_state_.br || now > pharah_update_ + pharah_boost_duration) {
        client_.mask(rock::button::middle, pharah_flight_duration);
        pharah_state_ = pharah_state::flight_button_released;
        pharah_update_ = now;
        return;
      }
    }

    // Handle right mouse button.
    if (mouse_state_.br) {
      // Keep middle mouse button mask set while right mouse button is pressed.
      if (pharah_state_ != pharah_state::flight_button_pressed) {
        // Set middle mouse button mask while right mouse button is pressed.
        client_.mask(rock::button::middle, pharah_flight_duration);
        pharah_state_ = pharah_state::flight_button_pressed;
        pharah_update_ = now;
      } else if (now > pharah_update_ + pharah_flight_duration / 2) {
        // Update middle mouse button mask timeout.
        client_.mask(rock::button::middle, pharah_flight_duration);
        pharah_update_ = now;
      }
      return;
    }

    // Do nothing if flight is still disabled.
    if (pharah_state_ == pharah_state::disabled) {
      return;
    }

    // Update middle mouse button mask timeout when right mouse button is released.
    if (pharah_state_ == pharah_state::flight_button_pressed) {
      client_.mask(rock::button::middle, std::chrono::seconds(0));
      pharah_state_ = pharah_state::flight_button_released;
      pharah_update_ = now - pharah_flight_duration;
      return;
    }

    assert(pharah_state_ == pharah_state::flight_button_released);

    // Limit automatic flight duration to 10 seconds.
    if (pharah_boost_ && now > pharah_boost_time_point_ + std::chrono::seconds(10)) {
      client_.mask(rock::button::middle, std::chrono::seconds(0));
      pharah_state_ = pharah_state::disabled;
      pharah_boost_ = false;
      return;
    }

    // Handle up/down buttons.
    if (mouse_state_.bu || mouse_state_.bd) {
      pharah_skip_ = true;
    }

    // Update middle mouse button mask timeout.
    auto update_duration = pharah_flight_duration + pharah_fall_duration;
    if (pharah_skip_) {
      update_duration += pharah_skip_delta;
    }
    if (now > pharah_update_ + update_duration) {
      if (pharah_skip_) {
        client_.mask(rock::button::middle, pharah_flight_duration - pharah_skip_delta);
      } else {
        client_.mask(rock::button::middle, pharah_flight_duration);
      }
      pharah_update_ = now;
      pharah_skip_ = false;
    }
    */
  }

  /*
  void update_pharah(clock::time_point now) noexcept
  {
    // TODO: Do not block right click during boost.
    // 
    // Handle control and Q keys.
    if (control_state.load(std::memory_order_relaxed) || q_state.load(std::memory_order_relaxed)) {
      // Unset middle mouse button mask when control key is pressed.
      if (pharah_state_ != pharah_state::disabled) {
        client_.mask(rock::button::middle, std::chrono::seconds(0));
        pharah_state_ = pharah_state::disabled;
      }
      return;
    }

    // Handle E key.
    if (e_state.load(std::memory_order_relaxed)) {
      pharah_state_ = pharah_state::flight_button_released;
      pharah_update_ = now - pharah_flight_duration - pharah_fall_duration;
      return;
    }

    // Handle shift key.
    if (shift_state.load(std::memory_order_relaxed)) {
      // Release middle mouse button until pharah_boost_duration time passes.
      if (pharah_state_ != pharah_state::rocket_button_pressed) {
        client_.mask(rock::button::middle, std::chrono::seconds(0));
        pharah_state_ = pharah_state::rocket_button_pressed;
        pharah_boost_time_point_ = now;
        pharah_boost_ = true;
        pharah_update_ = now;
      }
      return;
    }

    // Wait for boost to finish, then press middle mouse button.
    if (pharah_state_ == pharah_state::rocket_button_pressed) {
      if (mouse_state_.br || now > pharah_update_ + pharah_boost_duration) {
        client_.mask(rock::button::middle, pharah_flight_duration);
        pharah_state_ = pharah_state::flight_button_released;
        pharah_update_ = now;
        return;
      }
    }

    // Handle right mouse button.
    if (mouse_state_.br) {
      // Keep middle mouse button mask set while right mouse button is pressed.
      if (pharah_state_ != pharah_state::flight_button_pressed) {
        // Set middle mouse button mask while right mouse button is pressed.
        client_.mask(rock::button::middle, pharah_flight_duration);
        pharah_state_ = pharah_state::flight_button_pressed;
        pharah_update_ = now;
      } else if (now > pharah_update_ + pharah_flight_duration / 2) {
        // Update middle mouse button mask timeout.
        client_.mask(rock::button::middle, pharah_flight_duration);
        pharah_update_ = now;
      }
      return;
    }

    // Do nothing if flight is still disabled.
    if (pharah_state_ == pharah_state::disabled) {
      return;
    }

    // Update middle mouse button mask timeout when right mouse button is released.
    if (pharah_state_ == pharah_state::flight_button_pressed) {
      client_.mask(rock::button::middle, std::chrono::seconds(0));
      pharah_state_ = pharah_state::flight_button_released;
      pharah_update_ = now - pharah_flight_duration;
      return;
    }

    assert(pharah_state_ == pharah_state::flight_button_released);

    // Limit automatic flight duration to 10 seconds.
    if (pharah_boost_ && now > pharah_boost_time_point_ + std::chrono::seconds(10)) {
      client_.mask(rock::button::middle, std::chrono::seconds(0));
      pharah_state_ = pharah_state::disabled;
      pharah_boost_ = false;
      return;
    }

    // Handle up/down buttons.
    if (mouse_state_.bu || mouse_state_.bd) {
      pharah_skip_ = true;
    }

    // Update middle mouse button mask timeout.
    auto update_duration = pharah_flight_duration + pharah_fall_duration;
    if (pharah_skip_) {
      update_duration += pharah_skip_delta;
    }
    if (now > pharah_update_ + update_duration) {
      if (pharah_skip_) {
        client_.mask(rock::button::middle, pharah_flight_duration - pharah_skip_delta);
      } else {
        client_.mask(rock::button::middle, pharah_flight_duration);
      }
      pharah_update_ = now;
      pharah_skip_ = false;
    }
  }
  */

  void play_sound() noexcept
  {
    if (audio_device_) {
      SDL_QueueAudio(audio_device_, audio_buffer_, audio_length_);
      SDL_PauseAudioDevice(audio_device_, 0);
    }
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
          //client_.mask(rock::button::left, std::chrono::milliseconds(7));
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
  hid hid_;
  hid::mouse mouse_;
  rock::client client_;
  hero::hitscan hitscan_{ eye_, client_ };

  hero::type hero_ = hero::type::none;
  clock::time_point blocked_;
  bool ready_{ true };

  static inline std::atomic_bool screenshot_request{ false };
  static inline std::atomic_size_t screenshot_counter{ 0 };
  static inline std::shared_ptr<boost::asio::thread_pool> screenshot_thread_pool;

  Uint32 audio_length_{ 0 };
  Uint8* audio_buffer_{ nullptr };
  SDL_AudioSpec audio_spec_{};
  SDL_AudioDeviceID audio_device_{ 0 };

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

static HHOOK keyboard_hook = nullptr;

static LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wparam, LPARAM lparam)
{
  static constexpr auto size = 255;
  static constexpr auto name = std::string_view("Overwatch");
  static auto text = std::string(static_cast<size_t>(size), '\0');
  static auto alt_state = false;

  if (wparam == WM_KEYDOWN || wparam == WM_KEYUP) {
    const auto ks = reinterpret_cast<LPKBDLLHOOKSTRUCT>(lparam);
    if (ks->vkCode == VK_LMENU) {
      alt_state = wparam == WM_KEYDOWN;
    } else if (alt_state) {
      return CallNextHookEx(keyboard_hook, code, wparam, lparam);
    }
    if (ks->vkCode == 'E' || ks->vkCode == 'Q' || ks->vkCode == VK_LCONTROL || ks->vkCode == VK_LSHIFT)
    {
      if (const auto s = GetWindowText(GetForegroundWindow(), text.data(), size); s > 0) {
        if (std::string_view(text.data(), static_cast<std::size_t>(s)) == name) {
          switch (ks->vkCode) {
          case 'E':
            horus::plugin::input_e_state.store(wparam == WM_KEYDOWN, std::memory_order_release);
            break;
          case 'Q':
            horus::plugin::input_q_state.store(wparam == WM_KEYDOWN, std::memory_order_release);
            break;
          case VK_SPACE:
            horus::plugin::input_space_state.store(wparam == WM_KEYDOWN, std::memory_order_release);
            break;
          case VK_LSHIFT:
            horus::plugin::input_shift_state.store(wparam == WM_KEYDOWN, std::memory_order_release);
            break;
          case VK_LCONTROL:
            horus::plugin::input_control_state.store(wparam == WM_KEYDOWN, std::memory_order_release);
            break;
          }
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