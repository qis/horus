#include "obs.hpp"
#include <windows.h>

namespace horus {

#undef HORUS_OBS_IMPORT

#define HORUS_OBS_IMPORT(name) decltype(&::name) name = nullptr;

HORUS_OBS_IMPORT_ALL

#undef HORUS_OBS_IMPORT

#define HORUS_OBS_IMPORT(name)                                                \
  name = reinterpret_cast<decltype(&::name)>(GetProcAddress(library, #name)); \
  assert(name);

void obs_initialize() noexcept
{
  const auto library = GetModuleHandle("obs.dll");
  assert(library);
  HORUS_OBS_IMPORT_ALL
}

}  // namespace horus
