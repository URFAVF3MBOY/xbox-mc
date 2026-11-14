// just enough unistd.h for MobileGlues on windows
#pragma once
#include <io.h>
#include <process.h>
#include <direct.h>
#ifndef F_OK
#define F_OK 0
#define W_OK 2
#define R_OK 4
#endif
