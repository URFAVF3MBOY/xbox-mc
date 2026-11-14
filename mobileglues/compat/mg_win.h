// force-included into every MobileGlues file on windows (/FI)
#pragma once
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <time.h>
// posix mkdir(path, mode)
#define mkdir(path, mode) _mkdir(path)
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// clock_gettime isn't in the ucrt
#ifndef CLOCK_MONOTONIC
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#ifdef __cplusplus
extern "C" {
#endif
__declspec(dllimport) int __stdcall QueryPerformanceCounter(union _LARGE_INTEGER*);
__declspec(dllimport) int __stdcall QueryPerformanceFrequency(union _LARGE_INTEGER*);
#ifdef __cplusplus
}
#endif
static inline int clock_gettime(int clk, struct timespec* ts) {
    if (clk == CLOCK_REALTIME) return timespec_get(ts, TIME_UTC) ? 0 : -1;
    long long c, f;
    QueryPerformanceCounter((union _LARGE_INTEGER*)&c);
    QueryPerformanceFrequency((union _LARGE_INTEGER*)&f);
    ts->tv_sec = (time_t)(c / f);
    ts->tv_nsec = (long)((c % f) * 1000000000LL / f);
    return 0;
}
#endif

#ifdef __cplusplus
// other compilers get these through other headers
#include <array>
#include <stdexcept>
#endif
#endif
