#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(HAIHUNHOU_BUILD_SHARED)
#    define HAIHUNHOU_API __declspec(dllexport)
#  else
#    define HAIHUNHOU_API __declspec(dllimport)
#  endif
#else
#  define HAIHUNHOU_API __attribute__((visibility("default")))
#endif

#if defined(_MSC_VER)
#  define HAIHUNHOU_NOINLINE __declspec(noinline)
#  define HAIHUNHOU_INLINE __forceinline
#else
#  define HAIHUNHOU_NOINLINE __attribute__((noinline))
#  define HAIHUNHOU_INLINE inline __attribute__((always_inline))
#endif
