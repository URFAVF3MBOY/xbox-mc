// only used for a thread id in egl/trace.h
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define __NR_gettid 0
static inline long syscall(long) { return (long)GetCurrentThreadId(); }
