#pragma once

#define DRAW_OVERLAY 0
#define HERO_SUPPORT 0

#define HORUS_LOG "C:/OBS/horus.log"
#define HORUS_RES "C:/OBS/horus/res"

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