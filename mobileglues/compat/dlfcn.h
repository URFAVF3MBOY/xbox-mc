// dlfcn.h for windows (MobileGlues uses dlopen/dlsym to load the GLES driver)
// LoadPackagedLibrary in a uwp app, LoadLibrary otherwise.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define RTLD_LAZY 0x1
#define RTLD_NOW 0x2
#define RTLD_LOCAL 0x0
#define RTLD_GLOBAL 0x100
#define RTLD_DEFAULT ((void*)0)
#define RTLD_NEXT ((void*)-1)

#ifdef __cplusplus
extern "C" {
#endif

void* dlopen(const char* file, int mode);
void* dlsym(void* handle, const char* name);
int dlclose(void* handle);
char* dlerror(void);

#ifdef __cplusplus
}
#endif
