#pragma once

#ifdef HORUS_EXPORTS
#  define HORUS_API __declspec(dllexport)
#else
#  define HORUS_API __declspec(dllimport)
#endif