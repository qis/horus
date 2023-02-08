#pragma once
#include <SDL.h>
#include <memory>
#include <mutex>
#include <utility>

namespace horus {

class sound {
public:
  sound() noexcept = default;

  sound(const char* filename) noexcept
  {
    if (!handle) {
      std::lock_guard lock{ mutex };
      if (!handle) {
        handle = std::make_shared<library>();
      }
    }
    SDL_LoadWAV(filename, &spec_, &buffer_, &length_);
    if (buffer_) {
      device_ = SDL_OpenAudioDevice(nullptr, 0, &spec_, nullptr, 0);
    }
  }

  sound(sound&& other) noexcept :
    length_(std::exchange(other.length_, 0)),
    buffer_(std::exchange(other.buffer_, nullptr)),
    spec_(std::exchange(other.spec_, {})),
    device_(std::exchange(other.device_, 0))
  {}

  sound(const sound& other) noexcept = delete;

  sound& operator=(sound&& other) noexcept
  {
    close();
    length_ = std::exchange(other.length_, 0);
    buffer_ = std::exchange(other.buffer_, nullptr);
    spec_ = std::exchange(other.spec_, {});
    device_ = std::exchange(other.device_, 0);
    return *this;
  }

  sound& operator=(const sound& other) noexcept = delete;

  ~sound()
  {
    close();
  }

  void close() noexcept
  {
    if (buffer_) {
      if (device_) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
      }
      SDL_FreeWAV(buffer_);
      buffer_ = nullptr;
    }
  }

  void play() noexcept
  {
    if (device_) {
      SDL_QueueAudio(device_, buffer_, length_);
      SDL_PauseAudioDevice(device_, 0);
    }
  }

private:
  Uint32 length_{ 0 };
  Uint8* buffer_{ nullptr };
  SDL_AudioSpec spec_{};
  SDL_AudioDeviceID device_{ 0 };

  class library {
  public:
    library() noexcept
    {
      SDL_Init(SDL_INIT_AUDIO);
    }

    library(library&& other) = delete;
    library(const library& other) = delete;
    library& operator=(library&& other) = delete;
    library& operator=(const library& other) = delete;

    ~library()
    {
      SDL_Quit();
    }
  };

  static inline std::mutex mutex;
  static inline std::shared_ptr<library> handle;
  static inline bool initialized{ false };
};

}  // namespace horus