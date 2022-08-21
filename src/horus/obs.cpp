#include "obs.hpp"
#include <horus/log.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>

#include <windows.h>

namespace horus {

#undef HORUS_OBS_IMPORT

#define HORUS_OBS_IMPORT(name) decltype(&::name) name = nullptr;

HORUS_OBS_IMPORT_ALL

#undef HORUS_OBS_IMPORT

#define HORUS_OBS_IMPORT(name)                                                \
  name = reinterpret_cast<decltype(&::name)>(GetProcAddress(library, #name)); \
  if (!name) {                                                                \
    horus::log("could not load obs function: {}", #name);                     \
    return false;                                                             \
  }

bool initialize() noexcept
{
  const auto library = GetModuleHandle("obs.dll");
  if (!library) {
    horus::log("could not get module handle: {}", "obs.dll");
    return false;
  }
  HORUS_OBS_IMPORT_ALL
  return true;
}

}  // namespace horus
