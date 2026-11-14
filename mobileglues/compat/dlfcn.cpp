// dlopen/dlsym on windows
#include "dlfcn.h"
#include <string>
#include <mutex>
#include <vector>

static thread_local char g_err[512];
static std::mutex g_lock;
static std::vector<HMODULE> g_loaded; // for RTLD_DEFAULT lookups

static void set_error(const char* what, const char* name) {
    snprintf(g_err, sizeof(g_err), "%s %s failed (error %lu)", what, name ? name : "", GetLastError());
}

static HMODULE self_module() {
    HMODULE mod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&self_module), &mod);
    return mod;
}

extern "C" void* dlopen(const char* file, int) {
    if (!file) return self_module();
    std::wstring wide(file, file + strlen(file));
    for (auto& c : wide) if (c == L'/') c = L'\';
#if defined(WINAPI_FAMILY) && WINAPI_FAMILY == WINAPI_FAMILY_APP
    // in a uwp app only dlls inside the package can be loaded, by relative name
    HMODULE mod = LoadPackagedLibrary(wide.c_str(), 0);
#else
    HMODULE mod = LoadLibraryW(wide.c_str());
#endif
    if (!mod) { set_error("dlopen", file); return nullptr; }
    std::lock_guard<std::mutex> l(g_lock);
    g_loaded.push_back(mod);
    return mod;
}

extern "C" void* dlsym(void* handle, const char* name) {
    void* p = nullptr;
    if (handle == RTLD_DEFAULT || handle == RTLD_NEXT) {
        // own exports first (that's what glXGetProcAddress wants), then what we loaded
        p = reinterpret_cast<void*>(GetProcAddress(self_module(), name));
        if (!p) {
            std::lock_guard<std::mutex> l(g_lock);
            for (HMODULE m : g_loaded) {
                if ((p = reinterpret_cast<void*>(GetProcAddress(m, name)))) break;
            }
        }
    } else {
        p = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
    }
    if (!p) set_error("dlsym", name);
    return p;
}

extern "C" int dlclose(void* handle) {
    if (!handle) return 0;
    return FreeLibrary(static_cast<HMODULE>(handle)) ? 0 : -1;
}

extern "C" char* dlerror(void) {
    return g_err[0] ? g_err : nullptr;
}
