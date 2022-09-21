#pragma once

#define HORUS_LOGGER_LOG "C:/OBS/horus.log"
#define HORUS_EFFECT_DIR "C:/OBS/horus/res"
#define HORUS_HEROES_DIR "C:/OBS/horus/res/heroes"

#ifdef HORUS_EXPORTS
#  define HORUS_API __declspec(dllexport)
#else
#  define HORUS_API __declspec(dllimport)
#endif

#ifndef HORUS_DEBUG
#  ifdef NDEBUG
#    define HORUS_DEBUG 0
#  else
#    define HORUS_DEBUG 1
#  endif
#endif