#pragma once

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