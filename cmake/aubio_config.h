/* aubio config.h — minimal cross-platform static build, no external deps */
#pragma once
#define HAVE_STDLIB_H 1
#define HAVE_STDIO_H  1
#define HAVE_MATH_H   1
#define HAVE_STRING_H 1
#define HAVE_ERRNO_H  1
#define HAVE_LIMITS_H 1
#define HAVE_STDARG_H 1
/* Use C99 __VA_ARGS__ variadic macros — supported by GCC, Clang, and MSVC */
#define HAVE_C99_VARARGS_MACROS 1
/* HAVE_UNISTD_H omitted: not available on Windows */
#ifndef _WIN32
#define HAVE_UNISTD_H 1
#endif
/* no FFTW, no sndfile, no libav, no jack */
