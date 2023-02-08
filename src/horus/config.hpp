#pragma once
#include <chrono>

#ifdef HORUS_EXPORTS
#  define HORUS_API __declspec(dllexport)
#else
#  define HORUS_API __declspec(dllimport)
#endif

#define HORUS_HID_ADDRESS "192.168.178.7"
#define HORUS_HID_SERVICE "777"

namespace horus {

using clock = std::chrono::high_resolution_clock;

using std::chrono::nanoseconds;

template <class T>
using microseconds = std::chrono::duration<T, std::chrono::microseconds::period>;

template <class T>
using milliseconds = std::chrono::duration<T, std::chrono::milliseconds::period>;

template <class T>
using seconds = std::chrono::duration<T, std::chrono::seconds::period>;

template <class T>
using minutes = std::chrono::duration<T, std::chrono::minutes::period>;

template <class T>
using hours = std::chrono::duration<T, std::chrono::hours::period>;

using std::chrono::duration_cast;

}  // namespace horus