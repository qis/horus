#include "sound.hpp"
#include <filesystem>

namespace horus {

void announce(const char* name) noexcept
{
  static std::mutex mutex;
  static sound hero_sound;
  const auto filename = std::format("C:/OBS/horus/res/sounds/hero/{}.wav", name);
  if (std::filesystem::is_regular_file(filename)) {
    std::lock_guard lock{ mutex };
    hero_sound = { filename.data() };
    hero_sound.play();
  }
}

}  // namespace horus