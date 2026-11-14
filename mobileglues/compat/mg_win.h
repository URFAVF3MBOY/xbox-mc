// force-included into every MobileGlues file on windows (/FI)
#pragma once
#ifdef _WIN32
#include <direct.h>
#include <io.h>
// posix mkdir(path, mode)
#define mkdir(path, mode) _mkdir(path)
#endif
