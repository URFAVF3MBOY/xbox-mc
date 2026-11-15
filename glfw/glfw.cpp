// my own glfw3.dll for the xbox app (glfw 3.3 api, what lwjgl 3.2.2 binds)
// window = the app's CoreWindow, gl = EGL through mobileglues.dll,
// gamepads = Windows.Gaming.Input. one real window, everything else is faked
// or a no-op. the host calls xmcglfw_attach() on the ui thread first, then
// java calls the glfw functions from its own thread.
//
// CoreWindow events arrive on the ui thread, get queued, and are handed to the
// glfw callbacks inside glfwPollEvents on the java thread like real glfw does.

#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Devices.Input.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include <windows.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <functional>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <share.h>
#include <string>
#include <vector>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::UI::Core;
using winrt::Windows::Gaming::Input::Gamepad;
using winrt::Windows::Gaming::Input::GamepadButtons;
using winrt::Windows::System::VirtualKey;

#define API extern "C" __declspec(dllexport)

// ---- glfw constants (from the glfw 3.3 docs) ----
enum : int {
    GLFW_FALSE = 0, GLFW_TRUE = 1,
    GLFW_RELEASE = 0, GLFW_PRESS = 1, GLFW_REPEAT = 2,
    GLFW_DONT_CARE = -1,
    GLFW_KEY_UNKNOWN = -1, GLFW_KEY_LAST = 348,
    GLFW_MOUSE_BUTTON_LAST = 7,
    GLFW_JOYSTICK_LAST = 15,

    GLFW_NOT_INITIALIZED = 0x10001, GLFW_NO_CURRENT_CONTEXT = 0x10002, GLFW_INVALID_ENUM = 0x10003,
    GLFW_INVALID_VALUE = 0x10004, GLFW_API_UNAVAILABLE = 0x10006, GLFW_VERSION_UNAVAILABLE = 0x10007,
    GLFW_PLATFORM_ERROR = 0x10008, GLFW_FORMAT_UNAVAILABLE = 0x10009, GLFW_NO_WINDOW_CONTEXT = 0x1000A,

    GLFW_FOCUSED = 0x20001, GLFW_ICONIFIED = 0x20002, GLFW_RESIZABLE = 0x20003, GLFW_VISIBLE = 0x20004,
    GLFW_DECORATED = 0x20005, GLFW_AUTO_ICONIFY = 0x20006, GLFW_FLOATING = 0x20007, GLFW_MAXIMIZED = 0x20008,
    GLFW_CENTER_CURSOR = 0x20009, GLFW_TRANSPARENT_FRAMEBUFFER = 0x2000A, GLFW_HOVERED = 0x2000B,
    GLFW_FOCUS_ON_SHOW = 0x2000C,

    GLFW_RED_BITS = 0x21001, GLFW_GREEN_BITS = 0x21002, GLFW_BLUE_BITS = 0x21003, GLFW_ALPHA_BITS = 0x21004,
    GLFW_DEPTH_BITS = 0x21005, GLFW_STENCIL_BITS = 0x21006, GLFW_SAMPLES = 0x2100D,
    GLFW_SRGB_CAPABLE = 0x2100E, GLFW_REFRESH_RATE = 0x2100F, GLFW_DOUBLEBUFFER = 0x21010,

    GLFW_CLIENT_API = 0x22001, GLFW_CONTEXT_VERSION_MAJOR = 0x22002, GLFW_CONTEXT_VERSION_MINOR = 0x22003,
    GLFW_CONTEXT_REVISION = 0x22004, GLFW_CONTEXT_ROBUSTNESS = 0x22005, GLFW_OPENGL_FORWARD_COMPAT = 0x22006,
    GLFW_OPENGL_DEBUG_CONTEXT = 0x22007, GLFW_OPENGL_PROFILE = 0x22008, GLFW_CONTEXT_RELEASE_BEHAVIOR = 0x22009,
    GLFW_CONTEXT_NO_ERROR = 0x2200A, GLFW_CONTEXT_CREATION_API = 0x2200B,

    GLFW_NO_API = 0, GLFW_OPENGL_API = 0x30001, GLFW_OPENGL_ES_API = 0x30002,
    GLFW_OPENGL_ANY_PROFILE = 0, GLFW_OPENGL_CORE_PROFILE = 0x32001, GLFW_OPENGL_COMPAT_PROFILE = 0x32002,

    GLFW_CURSOR = 0x33001, GLFW_STICKY_KEYS = 0x33002, GLFW_STICKY_MOUSE_BUTTONS = 0x33003,
    GLFW_LOCK_KEY_MODS = 0x33004, GLFW_RAW_MOUSE_MOTION = 0x33005,
    GLFW_CURSOR_NORMAL = 0x34001, GLFW_CURSOR_HIDDEN = 0x34002, GLFW_CURSOR_DISABLED = 0x34003,

    GLFW_NATIVE_CONTEXT_API = 0x36001, GLFW_EGL_CONTEXT_API = 0x36002,

    GLFW_CONNECTED = 0x40001, GLFW_DISCONNECTED = 0x40002,
    GLFW_JOYSTICK_HAT_BUTTONS = 0x50001,

    GLFW_MOD_SHIFT = 0x1, GLFW_MOD_CONTROL = 0x2, GLFW_MOD_ALT = 0x4, GLFW_MOD_SUPER = 0x8,
    GLFW_MOD_CAPS_LOCK = 0x10, GLFW_MOD_NUM_LOCK = 0x20,
};

struct GLFWvidmode { int width, height, redBits, greenBits, blueBits, refreshRate; };
struct GLFWgammaramp { unsigned short *red, *green, *blue; unsigned int size; };
struct GLFWimage { int width, height; unsigned char* pixels; };
struct GLFWgamepadstate { unsigned char buttons[15]; float axes[6]; };

struct GLFWwindow;
struct GLFWmonitor;
struct GLFWcursor;

typedef void (*GLFWerrorfun)(int, const char*);
typedef void (*GLFWmonitorfun)(GLFWmonitor*, int);
typedef void (*GLFWjoystickfun)(int, int);
typedef void (*GLFWwindowposfun)(GLFWwindow*, int, int);
typedef void (*GLFWwindowsizefun)(GLFWwindow*, int, int);
typedef void (*GLFWwindowclosefun)(GLFWwindow*);
typedef void (*GLFWwindowrefreshfun)(GLFWwindow*);
typedef void (*GLFWwindowfocusfun)(GLFWwindow*, int);
typedef void (*GLFWwindowiconifyfun)(GLFWwindow*, int);
typedef void (*GLFWwindowmaximizefun)(GLFWwindow*, int);
typedef void (*GLFWframebuffersizefun)(GLFWwindow*, int, int);
typedef void (*GLFWwindowcontentscalefun)(GLFWwindow*, float, float);
typedef void (*GLFWkeyfun)(GLFWwindow*, int, int, int, int);
typedef void (*GLFWcharfun)(GLFWwindow*, unsigned int);
typedef void (*GLFWcharmodsfun)(GLFWwindow*, unsigned int, int);
typedef void (*GLFWmousebuttonfun)(GLFWwindow*, int, int, int);
typedef void (*GLFWcursorposfun)(GLFWwindow*, double, double);
typedef void (*GLFWcursorenterfun)(GLFWwindow*, int);
typedef void (*GLFWscrollfun)(GLFWwindow*, double, double);
typedef void (*GLFWdropfun)(GLFWwindow*, int, const char**);
typedef void (*GLFWglproc)();

namespace {

// ---- log (LocalState\glfw.log) ----
FILE* g_log = nullptr;
std::mutex g_logLock;

void Log(const char* fmt, ...)
{
    std::lock_guard<std::mutex> lock(g_logLock);
    if (!g_log) {
        try {
            std::wstring path{ winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path() };
            path += L"\\glfw.log";
            g_log = _wfsopen(path.c_str(), L"w", _SH_DENYNO);
        } catch (...) {}
        if (!g_log) return;
    }
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    va_end(args);
    fputc('\n', g_log);
    fflush(g_log);
}

// ---- errors ----
GLFWerrorfun g_errorCb = nullptr;
int g_lastError = 0;
char g_lastErrorText[512] = {};

void SetError(int code, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_lastErrorText, sizeof(g_lastErrorText), fmt, args);
    va_end(args);
    g_lastError = code;
    Log("error 0x%x: %s", code, g_lastErrorText);
    if (g_errorCb) g_errorCb(code, g_lastErrorText);
}

bool g_initialized = false;
#define REQUIRE_INIT(ret)                                                  \
    if (!g_initialized) {                                                  \
        SetError(GLFW_NOT_INITIALIZED, "glfw isn't initialized");          \
        return ret;                                                        \
    }

// ---- ui thread side ----
CoreWindow g_coreWindow{ nullptr };      // only touched on the ui thread
CoreDispatcher g_dispatcher{ nullptr };  // agile
std::atomic<double> g_scale{ 1.0 };      // raw pixels per view pixel
std::atomic<int> g_screenW{ 1920 }, g_screenH{ 1080 };
std::atomic<bool> g_attached{ false };
std::atomic<bool> g_hasSurface{ false };
unsigned g_uiButtons = 0;
wchar_t g_highSurrogate = 0;

enum class Ev { Key, Char, Button, Move, Delta, Scroll, Enter, Focus, Iconify, Close };
struct Event {
    Ev type;
    int a = 0, b = 0, c = 0, d = 0;
    double x = 0, y = 0;
};

std::mutex g_queueLock;
std::condition_variable g_queueCv;
std::vector<Event> g_queue;
bool g_wake = false;

void Push(Event const& e)
{
    {
        std::lock_guard<std::mutex> lock(g_queueLock);
        g_queue.push_back(e);
        g_wake = true;
    }
    g_queueCv.notify_all();
}

// windows virtual key -> glfw key
int MapKey(int vk, unsigned scan, bool extended)
{
    if (vk >= '0' && vk <= '9') return vk;
    if (vk >= 'A' && vk <= 'Z') return vk;
    if (vk >= 0x70 && vk <= 0x88) return 290 + (vk - 0x70);  // F1..F25
    if (vk >= 0x60 && vk <= 0x69) return 320 + (vk - 0x60);  // numpad 0..9
    switch (vk) {
    case 0x20: return 32;   // space
    case 0xDE: return 39;   // '
    case 0xBC: return 44;   // ,
    case 0xBD: return 45;   // -
    case 0xBE: return 46;   // .
    case 0xBF: return 47;   // /
    case 0xBA: return 59;   // ;
    case 0xBB: return 61;   // =
    case 0xDB: return 91;   // [
    case 0xDC: return 92;   // backslash
    case 0xDD: return 93;   // ]
    case 0xC0: return 96;   // `
    case 0xE2: return 162;  // world 2 (102nd key)
    case 0x1B: return 256;  // escape
    case 0x0D: return extended ? 335 : 257;  // kp enter / enter
    case 0x09: return 258;  // tab
    case 0x08: return 259;  // backspace
    case 0x2D: return 260;  // insert
    case 0x2E: return 261;  // delete
    case 0x27: return 262;  // right
    case 0x25: return 263;  // left
    case 0x28: return 264;  // down
    case 0x26: return 265;  // up
    case 0x21: return 266;  // page up
    case 0x22: return 267;  // page down
    case 0x24: return 268;  // home
    case 0x23: return 269;  // end
    case 0x14: return 280;  // caps lock
    case 0x91: return 281;  // scroll lock
    case 0x90: return 282;  // num lock
    case 0x2C: return 283;  // print screen
    case 0x13: return 284;  // pause
    case 0x6E: return 330;  // kp decimal
    case 0x6F: return 331;  // kp divide
    case 0x6A: return 332;  // kp multiply
    case 0x6D: return 333;  // kp subtract
    case 0x6B: return 334;  // kp add
    case 0x10: return scan == 0x36 ? 344 : 340;  // shift (right shift has its own scancode)
    case 0xA0: return 340;
    case 0xA1: return 344;
    case 0x11: return extended ? 345 : 341;  // control
    case 0xA2: return 341;
    case 0xA3: return 345;
    case 0x12: return extended ? 346 : 342;  // alt
    case 0xA4: return 342;
    case 0xA5: return 346;
    case 0x5B: return 343;  // left win
    case 0x5C: return 347;  // right win
    case 0x5D: return 348;  // menu
    }
    return GLFW_KEY_UNKNOWN;
}

int UiMods()
{
    auto down = [](VirtualKey k) {
        return (g_coreWindow.GetKeyState(k) & CoreVirtualKeyStates::Down) == CoreVirtualKeyStates::Down;
    };
    auto locked = [](VirtualKey k) {
        return (g_coreWindow.GetKeyState(k) & CoreVirtualKeyStates::Locked) == CoreVirtualKeyStates::Locked;
    };
    int mods = 0;
    if (down(VirtualKey::Shift)) mods |= GLFW_MOD_SHIFT;
    if (down(VirtualKey::Control)) mods |= GLFW_MOD_CONTROL;
    if (down(VirtualKey::Menu)) mods |= GLFW_MOD_ALT;
    if (down(VirtualKey::LeftWindows) || down(VirtualKey::RightWindows)) mods |= GLFW_MOD_SUPER;
    if (locked(VirtualKey::CapitalLock)) mods |= GLFW_MOD_CAPS_LOCK;
    if (locked(VirtualKey::NumberKeyLock)) mods |= GLFW_MOD_NUM_LOCK;
    return mods;
}

void UiKey(int vk, CorePhysicalKeyStatus const& st, bool down)
{
    // the xbox also sends the gamepad as virtual keys, that goes through wgi instead
    if (vk >= 195 && vk <= 218) return;
    int key = MapKey(vk, st.ScanCode, st.IsExtendedKey);
    int scancode = static_cast<int>(st.ScanCode) + (st.IsExtendedKey ? 0x100 : 0);
    int action = down ? (st.WasKeyDown ? GLFW_REPEAT : GLFW_PRESS) : GLFW_RELEASE;
    Push({ Ev::Key, key, scancode, action, UiMods() });
}

void UiPointer(winrt::Windows::UI::Input::PointerPoint const& p)
{
    double s = g_scale;
    auto pos = p.Position();
    Push({ Ev::Move, 0, 0, 0, 0, pos.X * s, pos.Y * s });

    auto props = p.Properties();
    unsigned now = 0;
    if (props.IsLeftButtonPressed()) now |= 1u << 0;
    if (props.IsRightButtonPressed()) now |= 1u << 1;
    if (props.IsMiddleButtonPressed()) now |= 1u << 2;
    if (props.IsXButton1Pressed()) now |= 1u << 3;
    if (props.IsXButton2Pressed()) now |= 1u << 4;
    unsigned changed = now ^ g_uiButtons;
    if (!changed) return;
    int mods = UiMods();
    for (int b = 0; b < 5; b++) {
        if (changed & (1u << b)) Push({ Ev::Button, b, (now & (1u << b)) ? GLFW_PRESS : GLFW_RELEASE, mods });
    }
    g_uiButtons = now;
}

void UiUpdateBounds()
{
    auto b = g_coreWindow.Bounds();
    double s = g_scale;
    g_screenW = static_cast<int>(b.Width * s + 0.5);
    g_screenH = static_cast<int>(b.Height * s + 0.5);
}

// ---- egl through mobileglues ----
struct Egl {
    HMODULE mg = nullptr;
    PFNEGLGETERRORPROC GetError;
    PFNEGLGETPLATFORMDISPLAYEXTPROC GetPlatformDisplayEXT;
    PFNEGLINITIALIZEPROC Initialize;
    PFNEGLTERMINATEPROC Terminate;
    PFNEGLQUERYSTRINGPROC QueryString;
    PFNEGLBINDAPIPROC BindAPI;
    PFNEGLCHOOSECONFIGPROC ChooseConfig;
    PFNEGLCREATEWINDOWSURFACEPROC CreateWindowSurface;
    PFNEGLCREATEPBUFFERSURFACEPROC CreatePbufferSurface;
    PFNEGLDESTROYSURFACEPROC DestroySurface;
    PFNEGLQUERYSURFACEPROC QuerySurface;
    PFNEGLCREATECONTEXTPROC CreateContext;
    PFNEGLDESTROYCONTEXTPROC DestroyContext;
    PFNEGLMAKECURRENTPROC MakeCurrent;
    PFNEGLSWAPBUFFERSPROC SwapBuffers;
    PFNEGLSWAPINTERVALPROC SwapInterval;
    void* (*GlProc)(const char*);
    EGLDisplay display = EGL_NO_DISPLAY;
} g_egl;

template <typename T> bool Resolve(T& fn, const char* name)
{
    fn = reinterpret_cast<T>(GetProcAddress(g_egl.mg, name));
    if (!fn) Log("mobileglues.dll has no %s", name);
    return fn != nullptr;
}

bool EnsureEgl()
{
    if (g_egl.display != EGL_NO_DISPLAY) return true;
    if (!g_egl.mg) {
        g_egl.mg = LoadPackagedLibrary(L"mobileglues.dll", 0);
        if (!g_egl.mg) {
            SetError(GLFW_API_UNAVAILABLE, "mobileglues.dll didn't load (error %lu)", GetLastError());
            return false;
        }
    }
    bool ok = Resolve(g_egl.GetError, "eglGetError") &&
              Resolve(g_egl.GetPlatformDisplayEXT, "eglGetPlatformDisplayEXT") &&
              Resolve(g_egl.Initialize, "eglInitialize") && Resolve(g_egl.Terminate, "eglTerminate") &&
              Resolve(g_egl.QueryString, "eglQueryString") && Resolve(g_egl.BindAPI, "eglBindAPI") &&
              Resolve(g_egl.ChooseConfig, "eglChooseConfig") &&
              Resolve(g_egl.CreateWindowSurface, "eglCreateWindowSurface") &&
              Resolve(g_egl.CreatePbufferSurface, "eglCreatePbufferSurface") &&
              Resolve(g_egl.DestroySurface, "eglDestroySurface") && Resolve(g_egl.QuerySurface, "eglQuerySurface") &&
              Resolve(g_egl.CreateContext, "eglCreateContext") &&
              Resolve(g_egl.DestroyContext, "eglDestroyContext") && Resolve(g_egl.MakeCurrent, "eglMakeCurrent") &&
              Resolve(g_egl.SwapBuffers, "eglSwapBuffers") && Resolve(g_egl.SwapInterval, "eglSwapInterval") &&
              Resolve(g_egl.GlProc, "glXGetProcAddress");
    if (!ok) {
        SetError(GLFW_API_UNAVAILABLE, "mobileglues.dll is missing egl functions");
        return false;
    }
    const EGLint attribs[] = {
        EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
#ifdef EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE
        EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE, EGL_TRUE,
#endif
#ifdef EGL_EXPERIMENTAL_PRESENT_PATH_ANGLE
        // draw straight into the swapchain instead of an extra texture that gets
        // copied (and flipped) every frame. angle flips y in its shaders instead
        EGL_EXPERIMENTAL_PRESENT_PATH_ANGLE, EGL_EXPERIMENTAL_PRESENT_PATH_FAST_ANGLE,
#endif
        EGL_NONE,
    };
    EGLDisplay dpy = g_egl.GetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, attribs);
    Log("angle present path: %s",
#ifdef EGL_EXPERIMENTAL_PRESENT_PATH_ANGLE
        "fast"
#else
        "copy"
#endif
    );
    EGLint major = 0, minor = 0;
    if (dpy == EGL_NO_DISPLAY || !g_egl.Initialize(dpy, &major, &minor)) {
        SetError(GLFW_API_UNAVAILABLE, "eglInitialize failed (0x%x)", g_egl.GetError());
        return false;
    }
    g_egl.display = dpy;
    Log("EGL %d.%d (%s)", major, minor, g_egl.QueryString(dpy, EGL_VENDOR));
    return true;
}

// ---- hints ----
struct Hints {
    int red, green, blue, alpha, depth, stencil, samples, srgb, doublebuffer, refresh;
    int visible, focused, resizable, decorated, maximized, autoIconify, floating, centerCursor, transparent,
        focusOnShow;
    int clientApi, creationApi, major, minor, profile, forward, debug, robustness, noError;
} g_hints;

void DefaultHints()
{
    g_hints = {};
    g_hints.red = g_hints.green = g_hints.blue = g_hints.alpha = 8;
    g_hints.depth = 24;
    g_hints.stencil = 8;
    g_hints.doublebuffer = GLFW_TRUE;
    g_hints.refresh = GLFW_DONT_CARE;
    g_hints.visible = g_hints.focused = g_hints.resizable = g_hints.decorated = GLFW_TRUE;
    g_hints.autoIconify = g_hints.centerCursor = g_hints.focusOnShow = GLFW_TRUE;
    g_hints.clientApi = GLFW_OPENGL_API;
    g_hints.creationApi = GLFW_NATIVE_CONTEXT_API;
    g_hints.major = 1;
    g_hints.minor = 0;
}

// ---- windows, monitor, cursors ----
struct Window {
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    bool onCoreWindow = false;
    int pbufW = 0, pbufH = 0;
    int clientApi = GLFW_OPENGL_API, major = 0, minor = 0, profile = 0, forward = 0, debug = 0;
    int shouldClose = 0;
    void* user = nullptr;
    int lastW = 0, lastH = 0;
    int focused = 1, iconified = 0, hovered = 0;
    int cursorMode = GLFW_CURSOR_NORMAL;
    int stickyKeys = 0, stickyButtons = 0, lockKeyMods = 0, rawMotion = 0;
    double cursorX = 0, cursorY = 0;
    char keys[GLFW_KEY_LAST + 1] = {};
    char buttons[GLFW_MOUSE_BUTTON_LAST + 1] = {};
    GLFWwindowposfun posCb = nullptr;
    GLFWwindowsizefun sizeCb = nullptr;
    GLFWwindowclosefun closeCb = nullptr;
    GLFWwindowrefreshfun refreshCb = nullptr;
    GLFWwindowfocusfun focusCb = nullptr;
    GLFWwindowiconifyfun iconifyCb = nullptr;
    GLFWwindowmaximizefun maximizeCb = nullptr;
    GLFWframebuffersizefun fbCb = nullptr;
    GLFWwindowcontentscalefun scaleCb = nullptr;
    GLFWkeyfun keyCb = nullptr;
    GLFWcharfun charCb = nullptr;
    GLFWcharmodsfun charModsCb = nullptr;
    GLFWmousebuttonfun buttonCb = nullptr;
    GLFWcursorposfun cursorCb = nullptr;
    GLFWcursorenterfun enterCb = nullptr;
    GLFWscrollfun scrollCb = nullptr;
    GLFWdropfun dropCb = nullptr;
};

Window* g_main = nullptr;                 // the one on the CoreWindow
std::vector<Window*> g_windows;
thread_local Window* t_current = nullptr;

struct Monitor { void* user = nullptr; } g_monitor;
GLFWmonitor* g_monitorList[1] = { reinterpret_cast<GLFWmonitor*>(&g_monitor) };
GLFWmonitorfun g_monitorCb = nullptr;
GLFWvidmode g_mode{};

struct Cursor { int shape; };

Window* W(GLFWwindow* w) { return reinterpret_cast<Window*>(w); }

void SurfaceSize(Window* win, int* w, int* h)
{
    if (win->surface != EGL_NO_SURFACE && win->onCoreWindow) {
        EGLint sw = 0, sh = 0;
        g_egl.QuerySurface(g_egl.display, win->surface, EGL_WIDTH, &sw);
        g_egl.QuerySurface(g_egl.display, win->surface, EGL_HEIGHT, &sh);
        *w = sw;
        *h = sh;
    } else if (win->onCoreWindow) {
        *w = g_screenW;
        *h = g_screenH;
    } else {
        *w = win->pbufW;
        *h = win->pbufH;
    }
}

// ---- gamepads ----
std::vector<Gamepad> g_pads;
void* g_padUser[GLFW_JOYSTICK_LAST + 1] = {};
GLFWjoystickfun g_joystickCb = nullptr;
int g_hatButtons = GLFW_TRUE;
float g_axes[GLFW_JOYSTICK_LAST + 1][6];
unsigned char g_jbuttons[GLFW_JOYSTICK_LAST + 1][19];
unsigned char g_hats[GLFW_JOYSTICK_LAST + 1][1];

void RefreshPads()
{
    std::vector<Gamepad> now;
    try {
        for (auto const& p : Gamepad::Gamepads()) {
            if (now.size() > GLFW_JOYSTICK_LAST) break;
            now.push_back(p);
        }
    } catch (...) {}
    size_t before = g_pads.size();
    g_pads = std::move(now);
    if (g_joystickCb && before != g_pads.size()) {
        for (size_t i = g_pads.size(); i < before; i++) g_joystickCb(static_cast<int>(i), GLFW_DISCONNECTED);
        for (size_t i = before; i < g_pads.size(); i++) g_joystickCb(static_cast<int>(i), GLFW_CONNECTED);
    }
}

bool ReadPad(int jid, GLFWgamepadstate* out)
{
    if (jid < 0 || jid >= static_cast<int>(g_pads.size())) return false;
    winrt::Windows::Gaming::Input::GamepadReading r;
    try {
        r = g_pads[jid].GetCurrentReading();
    } catch (...) {
        return false;
    }
    auto has = [&](GamepadButtons b) { return (r.Buttons & b) == b ? GLFW_PRESS : GLFW_RELEASE; };
    unsigned char* b = out->buttons;
    b[0] = has(GamepadButtons::A);
    b[1] = has(GamepadButtons::B);
    b[2] = has(GamepadButtons::X);
    b[3] = has(GamepadButtons::Y);
    b[4] = has(GamepadButtons::LeftShoulder);
    b[5] = has(GamepadButtons::RightShoulder);
    b[6] = has(GamepadButtons::View);
    b[7] = has(GamepadButtons::Menu);
    b[8] = GLFW_RELEASE;  // the guide button belongs to the system
    b[9] = has(GamepadButtons::LeftThumbstick);
    b[10] = has(GamepadButtons::RightThumbstick);
    b[11] = has(GamepadButtons::DPadUp);
    b[12] = has(GamepadButtons::DPadRight);
    b[13] = has(GamepadButtons::DPadDown);
    b[14] = has(GamepadButtons::DPadLeft);
    // glfw: y down is positive, triggers -1..1
    out->axes[0] = static_cast<float>(r.LeftThumbstickX);
    out->axes[1] = static_cast<float>(-r.LeftThumbstickY);
    out->axes[2] = static_cast<float>(r.RightThumbstickX);
    out->axes[3] = static_cast<float>(-r.RightThumbstickY);
    out->axes[4] = static_cast<float>(r.LeftTrigger * 2.0 - 1.0);
    out->axes[5] = static_cast<float>(r.RightTrigger * 2.0 - 1.0);
    return true;
}

// ---- controller as keyboard + mouse ----
// minecraft java has no controller support, so the first pad gets turned into
// the keys and mouse buttons minecraft already knows. in game (cursor captured)
// the right stick looks around, in menus the sticks move a cursor that
// glfwSwapBuffers draws, because the xbox has no mouse cursor.
namespace pad {
enum : int {
    K_SPACE = 32, K_A = 65, K_D = 68, K_E = 69, K_F = 70, K_Q = 81, K_S = 83, K_T = 84, K_W = 87,
    K_ESCAPE = 256, K_TAB = 258, K_RIGHT = 262, K_LEFT = 263, K_DOWN = 264, K_UP = 265,
    K_F5 = 294, K_LSHIFT = 340, K_LCTRL = 341,
};
constexpr int kMouse = 400;     // slots from here on are mouse buttons 0..2
constexpr int kSlots = kMouse + 3;
bool g_sent[kSlots] = {};       // what minecraft was told is held right now
bool g_prevLb = false, g_prevRb = false;
bool g_mouseUsed = false;       // a real mouse moved, then my cursor stays hidden
bool g_showCursor = false;
std::chrono::steady_clock::time_point g_last{};

float Curve(float v)
{
    float a = fabsf(v);
    if (a < 0.15f) return 0.0f;
    float t = (a - 0.15f) / 0.85f;
    return (v < 0 ? -t : t) * t;   // slow near the middle, fast at the edge
}
}

// ---- perf stats ----
// every 5 s: fps, cpu ms (time between swaps = minecraft + java + translation)
// and swap ms (time inside eglSwapBuffers = waiting for the gpu / vsync)
namespace perf {
using pclock = std::chrono::steady_clock;
pclock::time_point g_windowStart{}, g_lastSwapEnd{}, g_reportStart{};
int g_frames = 0;
double g_cpuMs = 0, g_swapMs = 0, g_worstMs = 0;
}

// ---- autoplay (benchmarks without touching the pc) ----
// LocalState\autoplay.txt, lines "<seconds> key <glfw key code>", seconds after
// the minecraft window got its title. presses the key for one poll.
namespace autoplay {
struct Step { double at; int key; bool done; };
std::vector<Step> g_steps;
bool g_loaded = false;
int g_releaseKey = -1;
}

void AutoplayLoad()
{
    using namespace autoplay;
    if (g_loaded) return;
    g_loaded = true;
    std::wstring path = std::wstring{ winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path() } + L"\\autoplay.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r") != 0 || !f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        double at = 0;
        int key = 0;
        if (sscanf_s(line, "%lf key %d", &at, &key) == 2) g_steps.push_back({ at, key, false });
    }
    fclose(f);
    Log("autoplay: %zu steps", g_steps.size());
}

void AutoplayTick(Window* win)
{
    using namespace autoplay;
    if (!win || g_steps.empty() || perf::g_windowStart.time_since_epoch().count() == 0) return;
    auto gw = reinterpret_cast<GLFWwindow*>(win);
    if (g_releaseKey >= 0) {
        win->keys[g_releaseKey] = GLFW_RELEASE;
        if (win->keyCb) win->keyCb(gw, g_releaseKey, 0, GLFW_RELEASE, 0);
        g_releaseKey = -1;
        return;
    }
    double t = std::chrono::duration<double>(perf::pclock::now() - perf::g_windowStart).count();
    for (auto& s : g_steps) {
        if (s.done || t < s.at) continue;
        s.done = true;
        Log("autoplay: key %d at %.1f s", s.key, t);
        win->keys[s.key] = GLFW_PRESS;
        if (win->keyCb) win->keyCb(gw, s.key, 0, GLFW_PRESS, 0);
        g_releaseKey = s.key;
        break;
    }
}

void PadInput(Window* win)
{
    using namespace pad;
    GLFWgamepadstate s{};
    if (!win || !ReadPad(0, &s)) {
        g_showCursor = false;
        return;
    }
    auto now = std::chrono::steady_clock::now();
    double dt = g_last.time_since_epoch().count() ? std::chrono::duration<double>(now - g_last).count() : 0.0;
    g_last = now;
    if (dt > 0.1) dt = 0.1;

    auto gw = reinterpret_cast<GLFWwindow*>(win);
    auto btn = [&](int b) { return s.buttons[b] == GLFW_PRESS; };
    float lx = s.axes[0], ly = s.axes[1], rx = s.axes[2], ry = s.axes[3];
    bool lt = s.axes[4] > -0.4f, rt = s.axes[5] > -0.4f;
    bool inGame = win->cursorMode == GLFW_CURSOR_DISABLED;

    bool want[kSlots] = {};
    if (inGame) {
        want[K_W] = ly < -0.35f;
        want[K_S] = ly > 0.35f;
        want[K_A] = lx < -0.35f;
        want[K_D] = lx > 0.35f;
        want[K_SPACE] = btn(0);       // A jump
        want[K_LSHIFT] = btn(1);      // B sneak
        want[K_E] = btn(2);           // X inventory
        want[K_F5] = btn(3);          // Y camera
        want[K_TAB] = btn(6);         // view: player list
        want[K_ESCAPE] = btn(7);      // menu: pause
        want[K_LCTRL] = btn(9);       // L3 sprint
        want[kMouse + 2] = btn(10);   // R3 pick block
        want[K_T] = btn(11);          // dpad up: chat
        want[K_Q] = btn(13);          // dpad down: drop
        want[K_F] = btn(14);          // dpad left: swap hands
        want[kMouse + 0] = rt;        // attack / break
        want[kMouse + 1] = lt;        // use / place
        float speed = 1100.0f * static_cast<float>(dt);
        double dx = Curve(rx) * speed, dy = Curve(ry) * speed;
        if (dx != 0 || dy != 0) {
            win->cursorX += dx;
            win->cursorY += dy;
            if (win->cursorCb) win->cursorCb(gw, win->cursorX, win->cursorY);
        }
    } else {
        want[kMouse + 0] = btn(0);                 // A click
        want[kMouse + 1] = btn(2);                 // X right click
        want[K_LSHIFT] = btn(3);                   // Y hold for shift click
        want[K_ESCAPE] = btn(1) || btn(7);         // B / menu: back
        want[K_UP] = btn(11);
        want[K_RIGHT] = btn(12);
        want[K_DOWN] = btn(13);
        want[K_LEFT] = btn(14);
        int sw = 0, sh = 0;
        SurfaceSize(win, &sw, &sh);
        float speed = sh * 1.1f * static_cast<float>(dt);
        double dx = (Curve(lx) + Curve(rx)) * speed, dy = (Curve(ly) + Curve(ry)) * speed;
        if (dx != 0 || dy != 0) {
            win->cursorX = (std::min)((std::max)(win->cursorX + dx, 0.0), sw - 1.0);
            win->cursorY = (std::min)((std::max)(win->cursorY + dy, 0.0), sh - 1.0);
            if (win->cursorCb) win->cursorCb(gw, win->cursorX, win->cursorY);
            g_mouseUsed = false;
        }
        if (btn(0) || btn(2)) g_mouseUsed = false;
    }
    g_showCursor = !inGame && !g_mouseUsed;

    // bumpers scroll: hotbar in game, lists in menus
    bool lb = btn(4), rb = btn(5);
    if (win->scrollCb) {
        if (lb && !g_prevLb) win->scrollCb(gw, 0.0, 1.0);
        if (rb && !g_prevRb) win->scrollCb(gw, 0.0, -1.0);
    }
    g_prevLb = lb;
    g_prevRb = rb;

    // keys first, so shift is already down when a shift click comes
    int mods = want[K_LSHIFT] ? GLFW_MOD_SHIFT : 0;
    for (int k = 0; k < kMouse; k++) {
        if (want[k] == g_sent[k]) continue;
        g_sent[k] = want[k];
        int action = want[k] ? GLFW_PRESS : GLFW_RELEASE;
        win->keys[k] = static_cast<char>(action);
        if (win->keyCb) win->keyCb(gw, k, 0, action, mods);
    }
    for (int b = 0; b < 3; b++) {
        if (want[kMouse + b] == g_sent[kMouse + b]) continue;
        g_sent[kMouse + b] = want[kMouse + b];
        int action = want[kMouse + b] ? GLFW_PRESS : GLFW_RELEASE;
        win->buttons[b] = static_cast<char>(action);
        if (win->buttonCb) win->buttonCb(gw, b, action, mods);
    }
}

// the menu cursor, drawn with scissored clears right before the swap so none of
// minecraft's gl state gets touched (everything changed is put back)
void DrawPadCursor(Window* win)
{
    if (!pad::g_showCursor || !win || win->cursorMode == GLFW_CURSOR_DISABLED || !g_egl.GlProc) return;
    using GetIv = void(APIENTRY*)(unsigned, int*);
    using GetFv = void(APIENTRY*)(unsigned, float*);
    using GetBv = void(APIENTRY*)(unsigned, unsigned char*);
    using IsEn = unsigned char(APIENTRY*)(unsigned);
    using En = void(APIENTRY*)(unsigned);
    using Sc = void(APIENTRY*)(int, int, int, int);
    using Cc = void(APIENTRY*)(float, float, float, float);
    using Cl = void(APIENTRY*)(unsigned);
    using Cm = void(APIENTRY*)(unsigned char, unsigned char, unsigned char, unsigned char);
    using Bf = void(APIENTRY*)(unsigned, unsigned);
    static GetIv getIv; static GetFv getFv; static GetBv getBv; static IsEn isEn;
    static En enable, disable; static Sc scissor; static Cc clearColor; static Cl clear;
    static Cm colorMask; static Bf bindFb;
    static bool ok = false, tried = false;
    if (!tried) {
        tried = true;
        auto p = [](const char* n) { return reinterpret_cast<void*>(g_egl.GlProc(n)); };
        getIv = (GetIv)p("glGetIntegerv"); getFv = (GetFv)p("glGetFloatv"); getBv = (GetBv)p("glGetBooleanv");
        isEn = (IsEn)p("glIsEnabled"); enable = (En)p("glEnable"); disable = (En)p("glDisable");
        scissor = (Sc)p("glScissor"); clearColor = (Cc)p("glClearColor"); clear = (Cl)p("glClear");
        colorMask = (Cm)p("glColorMask"); bindFb = (Bf)p("glBindFramebuffer");
        ok = getIv && getFv && getBv && isEn && enable && disable && scissor && clearColor && clear && colorMask && bindFb;
        Log("pad cursor: %s", ok ? "ready" : "gl functions missing");
    }
    if (!ok) return;

    const unsigned SCISSOR_TEST = 0x0C11, SCISSOR_BOX = 0x0C10, CLEAR_VALUE = 0x0C22, WRITEMASK = 0x0C23,
                   DRAW_FB_BINDING = 0x8CA6, DRAW_FB = 0x8CA9, COLOR_BIT = 0x4000;
    unsigned char hadScissor = isEn(SCISSOR_TEST);
    int box[4], fb = 0;
    float cc[4];
    unsigned char mask[4];
    getIv(SCISSOR_BOX, box);
    getFv(CLEAR_VALUE, cc);
    getBv(WRITEMASK, mask);
    getIv(DRAW_FB_BINDING, &fb);

    int sw = 0, sh = 0;
    SurfaceSize(win, &sw, &sh);
    bindFb(DRAW_FB, 0);
    enable(SCISSOR_TEST);
    colorMask(1, 1, 1, 1);
    static const char* arrow[] = {
        "X          ", "XX         ", "X.X        ", "X..X       ", "X...X      ", "X....X     ",
        "X.....X    ", "X......X   ", "X.......X  ", "X....XXXXX ", "X.XX.X     ", "XX  X.X    ",
        "     X.X   ", "      XX   ",
    };
    int px = (std::max)(2, sh / 300);
    int x0 = static_cast<int>(win->cursorX), y0 = static_cast<int>(win->cursorY);
    for (int pass = 0; pass < 2; pass++) {
        char want = pass == 0 ? 'X' : '.';
        float c = pass == 0 ? 0.0f : 1.0f;
        clearColor(c, c, c, 1.0f);
        for (int row = 0; row < 14; row++) {
            for (int col = 0; arrow[row][col]; col++) {
                if (arrow[row][col] != want) continue;
                scissor(x0 + col * px, sh - (y0 + (row + 1) * px), px, px);
                clear(COLOR_BIT);
            }
        }
    }
    bindFb(DRAW_FB, static_cast<unsigned>(fb));
    scissor(box[0], box[1], box[2], box[3]);
    clearColor(cc[0], cc[1], cc[2], cc[3]);
    colorMask(mask[0], mask[1], mask[2], mask[3]);
    if (!hadScissor) disable(SCISSOR_TEST);
}

// ---- time ----
LARGE_INTEGER g_freq{}, g_base{};

void DispatchEvents()
{
    std::vector<Event> events;
    {
        std::lock_guard<std::mutex> lock(g_queueLock);
        events.swap(g_queue);
        g_wake = false;
    }
    Window* win = g_main;
    if (!win) return;
    auto gw = reinterpret_cast<GLFWwindow*>(win);
    for (auto const& e : events) {
        int mods = win->lockKeyMods ? e.d : (e.d & ~(GLFW_MOD_CAPS_LOCK | GLFW_MOD_NUM_LOCK));
        switch (e.type) {
        case Ev::Key:
            if (e.a >= 0 && e.a <= GLFW_KEY_LAST) win->keys[e.a] = e.c == GLFW_RELEASE ? GLFW_RELEASE : GLFW_PRESS;
            if (win->keyCb) win->keyCb(gw, e.a, e.b, e.c, mods);
            break;
        case Ev::Char:
            if (e.a >= 32 && e.a != 127) {
                if (win->charCb) win->charCb(gw, static_cast<unsigned>(e.a));
                if (win->charModsCb) win->charModsCb(gw, static_cast<unsigned>(e.a), mods);
            }
            break;
        case Ev::Button:
            win->buttons[e.a] = static_cast<char>(e.b);
            if (win->buttonCb) win->buttonCb(gw, e.a, e.b, mods);
            break;
        case Ev::Move:
            if (win->cursorMode == GLFW_CURSOR_DISABLED) break;
            if (e.x == win->cursorX && e.y == win->cursorY) break;
            pad::g_mouseUsed = true;
            win->cursorX = e.x;
            win->cursorY = e.y;
            if (win->cursorCb) win->cursorCb(gw, e.x, e.y);
            break;
        case Ev::Delta:
            // relative mouse, only used while the cursor is captured
            if (win->cursorMode != GLFW_CURSOR_DISABLED) break;
            win->cursorX += e.x;
            win->cursorY += e.y;
            if (win->cursorCb) win->cursorCb(gw, win->cursorX, win->cursorY);
            break;
        case Ev::Scroll:
            if (win->scrollCb) win->scrollCb(gw, e.x, e.y);
            break;
        case Ev::Enter:
            win->hovered = e.a;
            if (win->enterCb) win->enterCb(gw, e.a);
            break;
        case Ev::Focus:
            win->focused = e.a;
            if (win->focusCb) win->focusCb(gw, e.a);
            break;
        case Ev::Iconify:
            win->iconified = e.a;
            if (win->iconifyCb) win->iconifyCb(gw, e.a);
            break;
        case Ev::Close:
            win->shouldClose = GLFW_TRUE;
            if (win->closeCb) win->closeCb(gw);
            break;
        }
    }

    int w = 0, h = 0;
    SurfaceSize(win, &w, &h);
    if (w != win->lastW || h != win->lastH) {
        win->lastW = w;
        win->lastH = h;
        Log("size %dx%d", w, h);
        if (win->sizeCb) win->sizeCb(gw, w, h);
        if (win->fbCb) win->fbCb(gw, w, h);
    }
}

void OnUi(std::function<void()> fn, bool wait)
{
    if (!g_dispatcher) return;
    auto op = g_dispatcher.RunAsync(CoreDispatcherPriority::Normal, [fn] { fn(); });
    if (wait) op.get();
}

} // namespace

// ============================================================================
// host side
// ============================================================================

// called by the host on the ui thread with its CoreWindow
API void xmcglfw_attach(void* coreWindow)
{
    winrt::copy_from_abi(g_coreWindow, coreWindow);
    g_dispatcher = g_coreWindow.Dispatcher();
    // on the xbox apps get put inside the tv safe area by default, which shifts
    // and crops the picture. use the whole screen, minecraft's gui already keeps
    // its stuff away from the edges
    try {
        auto view = winrt::Windows::UI::ViewManagement::ApplicationView::GetForCurrentView();
        bool ok = view.SetDesiredBoundsMode(winrt::Windows::UI::ViewManagement::ApplicationViewBoundsMode::UseCoreWindow);
        Log("full screen bounds: %s", ok ? "yes" : "no");
    } catch (...) {
        Log("full screen bounds: failed");
    }
    auto display = winrt::Windows::Graphics::Display::DisplayInformation::GetForCurrentView();
    g_scale = display.RawPixelsPerViewPixel();
    UiUpdateBounds();
    Log("attached, %d x %d px (scale %.2f)", g_screenW.load(), g_screenH.load(), g_scale.load());

    display.DpiChanged([](auto&& d, auto&&) {
        g_scale = d.RawPixelsPerViewPixel();
        UiUpdateBounds();
    });
    g_coreWindow.SizeChanged([](auto&&, auto&&) { UiUpdateBounds(); });
    g_coreWindow.KeyDown([](auto&&, KeyEventArgs const& a) { UiKey(static_cast<int>(a.VirtualKey()), a.KeyStatus(), true); });
    g_coreWindow.KeyUp([](auto&&, KeyEventArgs const& a) { UiKey(static_cast<int>(a.VirtualKey()), a.KeyStatus(), false); });
    // alt and f10 only show up here, not in KeyDown
    g_dispatcher.AcceleratorKeyActivated([](auto&&, AcceleratorKeyEventArgs const& a) {
        auto t = a.EventType();
        if (t == CoreAcceleratorKeyEventType::SystemKeyDown) UiKey(static_cast<int>(a.VirtualKey()), a.KeyStatus(), true);
        else if (t == CoreAcceleratorKeyEventType::SystemKeyUp) UiKey(static_cast<int>(a.VirtualKey()), a.KeyStatus(), false);
    });
    g_coreWindow.CharacterReceived([](auto&&, CharacterReceivedEventArgs const& a) {
        auto c = static_cast<wchar_t>(a.KeyCode());
        if (c >= 0xD800 && c <= 0xDBFF) {
            g_highSurrogate = c;
            return;
        }
        unsigned cp = c;
        if (c >= 0xDC00 && c <= 0xDFFF && g_highSurrogate) {
            cp = 0x10000 + ((g_highSurrogate - 0xD800) << 10) + (c - 0xDC00);
        }
        g_highSurrogate = 0;
        Push({ Ev::Char, static_cast<int>(cp), 0, 0, UiMods() });
    });
    g_coreWindow.PointerMoved([](auto&&, PointerEventArgs const& a) { UiPointer(a.CurrentPoint()); });
    g_coreWindow.PointerPressed([](auto&&, PointerEventArgs const& a) { UiPointer(a.CurrentPoint()); });
    g_coreWindow.PointerReleased([](auto&&, PointerEventArgs const& a) { UiPointer(a.CurrentPoint()); });
    g_coreWindow.PointerWheelChanged([](auto&&, PointerEventArgs const& a) {
        auto props = a.CurrentPoint().Properties();
        double d = props.MouseWheelDelta() / 120.0;
        if (props.IsHorizontalMouseWheel()) Push({ Ev::Scroll, 0, 0, 0, 0, d, 0 });
        else Push({ Ev::Scroll, 0, 0, 0, 0, 0, d });
    });
    g_coreWindow.PointerEntered([](auto&&, auto&&) { Push({ Ev::Enter, 1 }); });
    g_coreWindow.PointerExited([](auto&&, auto&&) { Push({ Ev::Enter, 0 }); });
    g_coreWindow.Activated([](auto&&, WindowActivatedEventArgs const& a) {
        Push({ Ev::Focus, a.WindowActivationState() != CoreWindowActivationState::Deactivated ? 1 : 0 });
    });
    g_coreWindow.VisibilityChanged([](auto&&, VisibilityChangedEventArgs const& a) { Push({ Ev::Iconify, a.Visible() ? 0 : 1 }); });
    g_coreWindow.Closed([](auto&&, auto&&) { Push({ Ev::Close }); });
    try {
        winrt::Windows::Devices::Input::MouseDevice::GetForCurrentView().MouseMoved([](auto&&, auto const& a) {
            auto d = a.MouseDelta();
            Push({ Ev::Delta, 0, 0, 0, 0, static_cast<double>(d.X), static_cast<double>(d.Y) });
        });
    } catch (...) {
        Log("no MouseDevice");
    }
    // B on the xbox means "back", which would leave the app
    SystemNavigationManager::GetForCurrentView().BackRequested([](auto&&, BackRequestedEventArgs const& a) { a.Handled(true); });
    g_attached = true;
}

API int xmcglfw_has_surface() { return g_hasSurface ? 1 : 0; }

// ============================================================================
// glfw api
// ============================================================================

API int glfwInit(void)
{
    if (g_initialized) return GLFW_TRUE;
    try {
        winrt::init_apartment(apartment_type::multi_threaded);
    } catch (...) {}
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_base);
    DefaultHints();
    RefreshPads();
    g_initialized = true;
    Log("glfwInit, attached=%d, %zu gamepad(s)", g_attached.load() ? 1 : 0, g_pads.size());
    return GLFW_TRUE;
}

API void glfwTerminate(void)
{
    if (!g_initialized) return;
    for (auto* w : g_windows) {
        if (w->context != EGL_NO_CONTEXT) g_egl.DestroyContext(g_egl.display, w->context);
        if (w->surface != EGL_NO_SURFACE) g_egl.DestroySurface(g_egl.display, w->surface);
        delete w;
    }
    g_windows.clear();
    g_main = nullptr;
    g_hasSurface = false;
    g_initialized = false;
    Log("glfwTerminate");
}

API void glfwInitHint(int hint, int value)
{
    if (hint == GLFW_JOYSTICK_HAT_BUTTONS) g_hatButtons = value;
}

API void glfwGetVersion(int* major, int* minor, int* rev)
{
    if (major) *major = 3;
    if (minor) *minor = 3;
    if (rev) *rev = 0;
}

API const char* glfwGetVersionString(void) { return "3.3.0 xbox-mc CoreWindow EGL"; }

API int glfwGetError(const char** description)
{
    int code = g_lastError;
    if (description) *description = code ? g_lastErrorText : nullptr;
    g_lastError = 0;
    return code;
}

API GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun cb)
{
    auto old = g_errorCb;
    g_errorCb = cb;
    return old;
}

// ---- monitor ----
API GLFWmonitor** glfwGetMonitors(int* count)
{
    if (count) *count = 1;
    return g_monitorList;
}

API GLFWmonitor* glfwGetPrimaryMonitor(void) { return g_monitorList[0]; }

API void glfwGetMonitorPos(GLFWmonitor*, int* x, int* y)
{
    if (x) *x = 0;
    if (y) *y = 0;
}

API void glfwGetMonitorWorkarea(GLFWmonitor*, int* x, int* y, int* w, int* h)
{
    if (x) *x = 0;
    if (y) *y = 0;
    if (w) *w = g_screenW;
    if (h) *h = g_screenH;
}

API void glfwGetMonitorPhysicalSize(GLFWmonitor*, int* w, int* h)
{
    // pretend it's a 40" tv
    if (w) *w = 885;
    if (h) *h = 498;
}

API void glfwGetMonitorContentScale(GLFWmonitor*, float* x, float* y)
{
    if (x) *x = 1.0f;
    if (y) *y = 1.0f;
}

API const char* glfwGetMonitorName(GLFWmonitor*) { return "Xbox display"; }
API void glfwSetMonitorUserPointer(GLFWmonitor* m, void* p) { reinterpret_cast<Monitor*>(m)->user = p; }
API void* glfwGetMonitorUserPointer(GLFWmonitor* m) { return reinterpret_cast<Monitor*>(m)->user; }

API GLFWmonitorfun glfwSetMonitorCallback(GLFWmonitorfun cb)
{
    auto old = g_monitorCb;
    g_monitorCb = cb;
    return old;
}

API const GLFWvidmode* glfwGetVideoMode(GLFWmonitor*)
{
    g_mode = { g_screenW, g_screenH, 8, 8, 8, 60 };
    return &g_mode;
}

API const GLFWvidmode* glfwGetVideoModes(GLFWmonitor* m, int* count)
{
    if (count) *count = 1;
    return glfwGetVideoMode(m);
}

API void glfwSetGamma(GLFWmonitor*, float) {}

API const GLFWgammaramp* glfwGetGammaRamp(GLFWmonitor*)
{
    static unsigned short ramp[256];
    static GLFWgammaramp gr{ ramp, ramp, ramp, 256 };
    for (int i = 0; i < 256; i++) ramp[i] = static_cast<unsigned short>(i * 257);
    return &gr;
}

API void glfwSetGammaRamp(GLFWmonitor*, const GLFWgammaramp*) {}

// ---- window hints ----
API void glfwDefaultWindowHints(void) { DefaultHints(); }

API void glfwWindowHint(int hint, int value)
{
    switch (hint) {
    case GLFW_RED_BITS: g_hints.red = value; break;
    case GLFW_GREEN_BITS: g_hints.green = value; break;
    case GLFW_BLUE_BITS: g_hints.blue = value; break;
    case GLFW_ALPHA_BITS: g_hints.alpha = value; break;
    case GLFW_DEPTH_BITS: g_hints.depth = value; break;
    case GLFW_STENCIL_BITS: g_hints.stencil = value; break;
    case GLFW_SAMPLES: g_hints.samples = value; break;
    case GLFW_SRGB_CAPABLE: g_hints.srgb = value; break;
    case GLFW_REFRESH_RATE: g_hints.refresh = value; break;
    case GLFW_DOUBLEBUFFER: g_hints.doublebuffer = value; break;
    case GLFW_VISIBLE: g_hints.visible = value; break;
    case GLFW_FOCUSED: g_hints.focused = value; break;
    case GLFW_RESIZABLE: g_hints.resizable = value; break;
    case GLFW_DECORATED: g_hints.decorated = value; break;
    case GLFW_MAXIMIZED: g_hints.maximized = value; break;
    case GLFW_AUTO_ICONIFY: g_hints.autoIconify = value; break;
    case GLFW_FLOATING: g_hints.floating = value; break;
    case GLFW_CENTER_CURSOR: g_hints.centerCursor = value; break;
    case GLFW_TRANSPARENT_FRAMEBUFFER: g_hints.transparent = value; break;
    case GLFW_FOCUS_ON_SHOW: g_hints.focusOnShow = value; break;
    case GLFW_CLIENT_API: g_hints.clientApi = value; break;
    case GLFW_CONTEXT_CREATION_API: g_hints.creationApi = value; break;
    case GLFW_CONTEXT_VERSION_MAJOR: g_hints.major = value; break;
    case GLFW_CONTEXT_VERSION_MINOR: g_hints.minor = value; break;
    case GLFW_OPENGL_PROFILE: g_hints.profile = value; break;
    case GLFW_OPENGL_FORWARD_COMPAT: g_hints.forward = value; break;
    case GLFW_OPENGL_DEBUG_CONTEXT: g_hints.debug = value; break;
    case GLFW_CONTEXT_ROBUSTNESS: g_hints.robustness = value; break;
    case GLFW_CONTEXT_NO_ERROR: g_hints.noError = value; break;
    default: break;  // the win32/cocoa/x11 specific ones don't matter here
    }
}

API void glfwWindowHintString(int, const char*) {}

// ---- windows ----
API GLFWwindow* glfwCreateWindow(int width, int height, const char* title, GLFWmonitor*, GLFWwindow* share)
{
    REQUIRE_INIT(nullptr)
    Log("glfwCreateWindow(%d, %d, \"%s\") api=0x%x version=%d.%d profile=0x%x", width, height, title ? title : "",
        g_hints.clientApi, g_hints.major, g_hints.minor, g_hints.profile);
    if (g_hints.clientApi == GLFW_NO_API) {
        SetError(GLFW_API_UNAVAILABLE, "only gl/gles windows work here");
        return nullptr;
    }
    if (!EnsureEgl()) return nullptr;

    bool es = g_hints.clientApi == GLFW_OPENGL_ES_API;
    if (!g_egl.BindAPI(es ? EGL_OPENGL_ES_API : EGL_OPENGL_API)) {
        SetError(GLFW_API_UNAVAILABLE, "eglBindAPI failed (0x%x)", g_egl.GetError());
        return nullptr;
    }

    bool onCoreWindow = !g_main && g_attached;
    std::vector<EGLint> ca = {
        EGL_SURFACE_TYPE, onCoreWindow ? EGL_WINDOW_BIT : EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, es ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_BIT,
    };
    auto bits = [&](EGLint attr, int v) {
        if (v != GLFW_DONT_CARE) ca.insert(ca.end(), { attr, v });
    };
    bits(EGL_RED_SIZE, g_hints.red);
    bits(EGL_GREEN_SIZE, g_hints.green);
    bits(EGL_BLUE_SIZE, g_hints.blue);
    bits(EGL_ALPHA_SIZE, g_hints.alpha);
    bits(EGL_DEPTH_SIZE, g_hints.depth);
    bits(EGL_STENCIL_SIZE, g_hints.stencil);
    if (g_hints.samples > 0) ca.insert(ca.end(), { EGL_SAMPLE_BUFFERS, 1, EGL_SAMPLES, g_hints.samples });
    ca.push_back(EGL_NONE);

    EGLConfig config = nullptr;
    EGLint count = 0;
    if (!g_egl.ChooseConfig(g_egl.display, ca.data(), &config, 1, &count) || count < 1) {
        SetError(GLFW_FORMAT_UNAVAILABLE, "no EGL config for that pixel format (0x%x)", g_egl.GetError());
        return nullptr;
    }

    auto* win = new Window;
    win->onCoreWindow = onCoreWindow;
    win->clientApi = g_hints.clientApi;
    win->major = g_hints.major;
    win->minor = g_hints.minor;
    win->profile = g_hints.profile;
    win->forward = g_hints.forward;
    win->debug = g_hints.debug;

    if (onCoreWindow) {
        // angle hooks CoreWindow events when it makes the surface, that has to
        // happen on the ui thread
        EGLSurface surface = EGL_NO_SURFACE;
        EGLint err = EGL_SUCCESS;
        OnUi([&] {
            surface = g_egl.CreateWindowSurface(g_egl.display, config,
                                                reinterpret_cast<EGLNativeWindowType>(get_abi(g_coreWindow)), nullptr);
            if (surface == EGL_NO_SURFACE) err = g_egl.GetError();
        }, true);
        win->surface = surface;
        if (surface == EGL_NO_SURFACE) {
            SetError(GLFW_PLATFORM_ERROR, "eglCreateWindowSurface failed (0x%x)", err);
            delete win;
            return nullptr;
        }
    } else {
        win->pbufW = width > 0 ? width : 1;
        win->pbufH = height > 0 ? height : 1;
        const EGLint pa[] = { EGL_WIDTH, win->pbufW, EGL_HEIGHT, win->pbufH, EGL_NONE };
        win->surface = g_egl.CreatePbufferSurface(g_egl.display, config, pa);
        if (win->surface == EGL_NO_SURFACE) {
            SetError(GLFW_PLATFORM_ERROR, "eglCreatePbufferSurface failed (0x%x)", g_egl.GetError());
            delete win;
            return nullptr;
        }
    }

    std::vector<EGLint> xa;
    if (es) {
        xa.insert(xa.end(), { EGL_CONTEXT_MAJOR_VERSION, g_hints.major < 2 ? 2 : g_hints.major,
                              EGL_CONTEXT_MINOR_VERSION, g_hints.major < 2 ? 0 : g_hints.minor });
    } else {
        // 1.0 is glfw's default and means "anything"
        if (g_hints.major != 1 || g_hints.minor != 0) {
            xa.insert(xa.end(), { EGL_CONTEXT_MAJOR_VERSION, g_hints.major, EGL_CONTEXT_MINOR_VERSION, g_hints.minor });
        }
        if (g_hints.profile == GLFW_OPENGL_CORE_PROFILE)
            xa.insert(xa.end(), { EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT });
        else if (g_hints.profile == GLFW_OPENGL_COMPAT_PROFILE)
            xa.insert(xa.end(), { EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT });
        if (g_hints.forward) xa.insert(xa.end(), { EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE, EGL_TRUE });
    }
    if (g_hints.debug) xa.insert(xa.end(), { EGL_CONTEXT_OPENGL_DEBUG, EGL_TRUE });
    xa.push_back(EGL_NONE);

    EGLContext shareCtx = share ? W(share)->context : EGL_NO_CONTEXT;
    win->context = g_egl.CreateContext(g_egl.display, config, shareCtx, xa.data());
    if (win->context == EGL_NO_CONTEXT) {
        EGLint err = g_egl.GetError();
        g_egl.DestroySurface(g_egl.display, win->surface);
        delete win;
        SetError(err == EGL_BAD_MATCH ? GLFW_VERSION_UNAVAILABLE : GLFW_PLATFORM_ERROR,
                 "eglCreateContext failed (0x%x)", err);
        return nullptr;
    }

    int realW = 0, realH = 0;
    SurfaceSize(win, &realW, &realH);
    if (onCoreWindow) {
        g_main = win;
        g_hasSurface = true;
        // the CoreWindow is always full size, not what was asked for. remember the
        // asked size, so the first glfwPollEvents sees the difference and sends the
        // real size to the size callbacks (minecraft only learns it from there,
        // otherwise the picture and the mouse are scaled for 854x480)
        win->lastW = width;
        win->lastH = height;
    }
    g_windows.push_back(win);
    Log("window %p made (%s, %dx%d, asked for %dx%d)", win, onCoreWindow ? "CoreWindow" : "pbuffer", realW, realH, width, height);
    return reinterpret_cast<GLFWwindow*>(win);
}

API void glfwDestroyWindow(GLFWwindow* w)
{
    Window* win = W(w);
    if (!win) return;
    if (t_current == win) {
        g_egl.MakeCurrent(g_egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        t_current = nullptr;
    }
    g_egl.DestroyContext(g_egl.display, win->context);
    g_egl.DestroySurface(g_egl.display, win->surface);
    for (size_t i = 0; i < g_windows.size(); i++) {
        if (g_windows[i] == win) {
            g_windows.erase(g_windows.begin() + i);
            break;
        }
    }
    if (g_main == win) {
        g_main = nullptr;
        g_hasSurface = false;
    }
    delete win;
}

API int glfwWindowShouldClose(GLFWwindow* w) { return W(w)->shouldClose; }
API void glfwSetWindowShouldClose(GLFWwindow* w, int value) { W(w)->shouldClose = value; }
API void glfwSetWindowTitle(GLFWwindow*, const char* title)
{
    Log("title: %s", title ? title : "");
    // the minecraft window: start the clock for autoplay
    if (title && strstr(title, "Minecraft") && perf::g_windowStart.time_since_epoch().count() == 0) {
        perf::g_windowStart = perf::pclock::now();
        AutoplayLoad();
    }
}
API void glfwSetWindowIcon(GLFWwindow*, int, const GLFWimage*) {}

API void glfwGetWindowPos(GLFWwindow*, int* x, int* y)
{
    if (x) *x = 0;
    if (y) *y = 0;
}

API void glfwSetWindowPos(GLFWwindow*, int, int) {}

API void glfwGetWindowSize(GLFWwindow* w, int* width, int* height)
{
    int sw = 0, sh = 0;
    SurfaceSize(W(w), &sw, &sh);
    if (width) *width = sw;
    if (height) *height = sh;
}

API void glfwSetWindowSizeLimits(GLFWwindow*, int, int, int, int) {}
API void glfwSetWindowAspectRatio(GLFWwindow*, int, int) {}
API void glfwSetWindowSize(GLFWwindow*, int, int) {}

API void glfwGetFramebufferSize(GLFWwindow* w, int* width, int* height) { glfwGetWindowSize(w, width, height); }

API void glfwGetWindowFrameSize(GLFWwindow*, int* l, int* t, int* r, int* b)
{
    if (l) *l = 0;
    if (t) *t = 0;
    if (r) *r = 0;
    if (b) *b = 0;
}

API void glfwGetWindowContentScale(GLFWwindow*, float* x, float* y)
{
    if (x) *x = 1.0f;
    if (y) *y = 1.0f;
}

API float glfwGetWindowOpacity(GLFWwindow*) { return 1.0f; }
API void glfwSetWindowOpacity(GLFWwindow*, float) {}
API void glfwIconifyWindow(GLFWwindow*) {}
API void glfwRestoreWindow(GLFWwindow*) {}
API void glfwMaximizeWindow(GLFWwindow*) {}
API void glfwShowWindow(GLFWwindow*) {}
API void glfwHideWindow(GLFWwindow*) {}
API void glfwFocusWindow(GLFWwindow*) {}
API void glfwRequestWindowAttention(GLFWwindow*) {}

// always "fullscreen" on the one monitor
API GLFWmonitor* glfwGetWindowMonitor(GLFWwindow* w) { return W(w)->onCoreWindow ? g_monitorList[0] : nullptr; }
API void glfwSetWindowMonitor(GLFWwindow*, GLFWmonitor*, int, int, int, int, int) {}

API int glfwGetWindowAttrib(GLFWwindow* w, int attrib)
{
    Window* win = W(w);
    switch (attrib) {
    case GLFW_FOCUSED: return win->focused;
    case GLFW_ICONIFIED: return win->iconified;
    case GLFW_VISIBLE: return win->onCoreWindow ? GLFW_TRUE : GLFW_FALSE;
    case GLFW_HOVERED: return win->hovered;
    case GLFW_MAXIMIZED: return win->onCoreWindow ? GLFW_TRUE : GLFW_FALSE;
    case GLFW_RESIZABLE: return GLFW_FALSE;
    case GLFW_DECORATED: return GLFW_FALSE;
    case GLFW_FLOATING: return GLFW_FALSE;
    case GLFW_AUTO_ICONIFY: return GLFW_TRUE;
    case GLFW_TRANSPARENT_FRAMEBUFFER: return GLFW_FALSE;
    case GLFW_FOCUS_ON_SHOW: return GLFW_TRUE;
    case GLFW_CLIENT_API: return win->clientApi;
    case GLFW_CONTEXT_CREATION_API: return GLFW_EGL_CONTEXT_API;
    // mobileglues gives 4.0 when nothing is asked for
    case GLFW_CONTEXT_VERSION_MAJOR: return (win->major == 1 && win->minor == 0) ? 4 : win->major;
    case GLFW_CONTEXT_VERSION_MINOR: return (win->major == 1 && win->minor == 0) ? 0 : win->minor;
    case GLFW_CONTEXT_REVISION: return 0;
    case GLFW_OPENGL_PROFILE: return win->profile;
    case GLFW_OPENGL_FORWARD_COMPAT: return win->forward;
    case GLFW_OPENGL_DEBUG_CONTEXT: return win->debug;
    case GLFW_CONTEXT_ROBUSTNESS: return 0;
    case GLFW_CONTEXT_RELEASE_BEHAVIOR: return 0;
    case GLFW_CONTEXT_NO_ERROR: return GLFW_FALSE;
    }
    SetError(GLFW_INVALID_ENUM, "unknown window attribute 0x%x", attrib);
    return 0;
}

API void glfwSetWindowAttrib(GLFWwindow*, int, int) {}
API void glfwSetWindowUserPointer(GLFWwindow* w, void* p) { W(w)->user = p; }
API void* glfwGetWindowUserPointer(GLFWwindow* w) { return W(w)->user; }

#define CALLBACK_SETTER(name, type, field)        \
    API type name(GLFWwindow* w, type cb)         \
    {                                             \
        type old = W(w)->field;                   \
        W(w)->field = cb;                         \
        return old;                               \
    }

CALLBACK_SETTER(glfwSetWindowPosCallback, GLFWwindowposfun, posCb)
CALLBACK_SETTER(glfwSetWindowSizeCallback, GLFWwindowsizefun, sizeCb)
CALLBACK_SETTER(glfwSetWindowCloseCallback, GLFWwindowclosefun, closeCb)
CALLBACK_SETTER(glfwSetWindowRefreshCallback, GLFWwindowrefreshfun, refreshCb)
CALLBACK_SETTER(glfwSetWindowFocusCallback, GLFWwindowfocusfun, focusCb)
CALLBACK_SETTER(glfwSetWindowIconifyCallback, GLFWwindowiconifyfun, iconifyCb)
CALLBACK_SETTER(glfwSetWindowMaximizeCallback, GLFWwindowmaximizefun, maximizeCb)
CALLBACK_SETTER(glfwSetFramebufferSizeCallback, GLFWframebuffersizefun, fbCb)
CALLBACK_SETTER(glfwSetWindowContentScaleCallback, GLFWwindowcontentscalefun, scaleCb)
CALLBACK_SETTER(glfwSetKeyCallback, GLFWkeyfun, keyCb)
CALLBACK_SETTER(glfwSetCharCallback, GLFWcharfun, charCb)
CALLBACK_SETTER(glfwSetCharModsCallback, GLFWcharmodsfun, charModsCb)
CALLBACK_SETTER(glfwSetMouseButtonCallback, GLFWmousebuttonfun, buttonCb)
CALLBACK_SETTER(glfwSetCursorPosCallback, GLFWcursorposfun, cursorCb)
CALLBACK_SETTER(glfwSetCursorEnterCallback, GLFWcursorenterfun, enterCb)
CALLBACK_SETTER(glfwSetScrollCallback, GLFWscrollfun, scrollCb)
CALLBACK_SETTER(glfwSetDropCallback, GLFWdropfun, dropCb)

// ---- events ----
API void glfwPollEvents(void)
{
    REQUIRE_INIT(void())
    RefreshPads();
    DispatchEvents();
    PadInput(g_main);
    AutoplayTick(g_main);
}

API void glfwWaitEventsTimeout(double timeout)
{
    REQUIRE_INIT(void())
    {
        std::unique_lock<std::mutex> lock(g_queueLock);
        if (timeout < 0) g_queueCv.wait(lock, [] { return g_wake; });
        else g_queueCv.wait_for(lock, std::chrono::duration<double>(timeout), [] { return g_wake; });
    }
    glfwPollEvents();
}

API void glfwWaitEvents(void) { glfwWaitEventsTimeout(-1.0); }

API void glfwPostEmptyEvent(void)
{
    {
        std::lock_guard<std::mutex> lock(g_queueLock);
        g_wake = true;
    }
    g_queueCv.notify_all();
}

// ---- input ----
API int glfwGetInputMode(GLFWwindow* w, int mode)
{
    Window* win = W(w);
    switch (mode) {
    case GLFW_CURSOR: return win->cursorMode;
    case GLFW_STICKY_KEYS: return win->stickyKeys;
    case GLFW_STICKY_MOUSE_BUTTONS: return win->stickyButtons;
    case GLFW_LOCK_KEY_MODS: return win->lockKeyMods;
    case GLFW_RAW_MOUSE_MOTION: return win->rawMotion;
    }
    SetError(GLFW_INVALID_ENUM, "unknown input mode 0x%x", mode);
    return 0;
}

API void glfwSetInputMode(GLFWwindow* w, int mode, int value)
{
    Window* win = W(w);
    switch (mode) {
    case GLFW_CURSOR:
        if (win->cursorMode == value) return;
        win->cursorMode = value;
        if (win->onCoreWindow) {
            OnUi([value] {
                if (value == GLFW_CURSOR_NORMAL) g_coreWindow.PointerCursor(CoreCursor(CoreCursorType::Arrow, 0));
                else g_coreWindow.PointerCursor(nullptr);
            }, false);
        }
        if (value == GLFW_CURSOR_DISABLED) {
            int sw = 0, sh = 0;
            SurfaceSize(win, &sw, &sh);
            win->cursorX = sw / 2.0;
            win->cursorY = sh / 2.0;
        }
        break;
    case GLFW_STICKY_KEYS: win->stickyKeys = value; break;
    case GLFW_STICKY_MOUSE_BUTTONS: win->stickyButtons = value; break;
    case GLFW_LOCK_KEY_MODS: win->lockKeyMods = value; break;
    case GLFW_RAW_MOUSE_MOTION: win->rawMotion = value; break;
    default: SetError(GLFW_INVALID_ENUM, "unknown input mode 0x%x", mode); break;
    }
}

API int glfwRawMouseMotionSupported(void) { return GLFW_TRUE; }
API const char* glfwGetKeyName(int, int) { return nullptr; }
API int glfwGetKeyScancode(int) { return -1; }

API int glfwGetKey(GLFWwindow* w, int key)
{
    if (key < 0 || key > GLFW_KEY_LAST) return GLFW_RELEASE;
    return W(w)->keys[key];
}

API int glfwGetMouseButton(GLFWwindow* w, int button)
{
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return GLFW_RELEASE;
    return W(w)->buttons[button];
}

API void glfwGetCursorPos(GLFWwindow* w, double* x, double* y)
{
    if (x) *x = W(w)->cursorX;
    if (y) *y = W(w)->cursorY;
}

API void glfwSetCursorPos(GLFWwindow* w, double x, double y)
{
    // the real pointer can't be moved in a uwp app, only the virtual one
    W(w)->cursorX = x;
    W(w)->cursorY = y;
}

API GLFWcursor* glfwCreateCursor(const GLFWimage*, int, int) { return reinterpret_cast<GLFWcursor*>(new Cursor{ 0 }); }
API GLFWcursor* glfwCreateStandardCursor(int shape) { return reinterpret_cast<GLFWcursor*>(new Cursor{ shape }); }
API void glfwDestroyCursor(GLFWcursor* c) { delete reinterpret_cast<Cursor*>(c); }
API void glfwSetCursor(GLFWwindow*, GLFWcursor*) {}

// ---- joysticks (every xbox controller is a gamepad) ----
API int glfwJoystickPresent(int jid) { return jid >= 0 && jid < static_cast<int>(g_pads.size()) ? GLFW_TRUE : GLFW_FALSE; }

API const float* glfwGetJoystickAxes(int jid, int* count)
{
    GLFWgamepadstate s{};
    if (!ReadPad(jid, &s)) {
        if (count) *count = 0;
        return nullptr;
    }
    memcpy(g_axes[jid], s.axes, sizeof(s.axes));
    if (count) *count = 6;
    return g_axes[jid];
}

API const unsigned char* glfwGetJoystickButtons(int jid, int* count)
{
    GLFWgamepadstate s{};
    if (!ReadPad(jid, &s)) {
        if (count) *count = 0;
        return nullptr;
    }
    memcpy(g_jbuttons[jid], s.buttons, 15);
    int n = 15;
    if (g_hatButtons) {
        // glfw puts the hat at the end as up, right, down, left
        g_jbuttons[jid][15] = s.buttons[11];
        g_jbuttons[jid][16] = s.buttons[12];
        g_jbuttons[jid][17] = s.buttons[13];
        g_jbuttons[jid][18] = s.buttons[14];
        n = 19;
    }
    if (count) *count = n;
    return g_jbuttons[jid];
}

API const unsigned char* glfwGetJoystickHats(int jid, int* count)
{
    GLFWgamepadstate s{};
    if (!ReadPad(jid, &s)) {
        if (count) *count = 0;
        return nullptr;
    }
    unsigned char h = 0;
    if (s.buttons[11]) h |= 1;
    if (s.buttons[12]) h |= 2;
    if (s.buttons[13]) h |= 4;
    if (s.buttons[14]) h |= 8;
    g_hats[jid][0] = h;
    if (count) *count = 1;
    return g_hats[jid];
}

API const char* glfwGetJoystickName(int jid) { return glfwJoystickPresent(jid) ? "Xbox Controller" : nullptr; }

// same guid glfw uses for xinput pads, so mapping databases treat it like one
API const char* glfwGetJoystickGUID(int jid) { return glfwJoystickPresent(jid) ? "78696e70757401000000000000000000" : nullptr; }

API void glfwSetJoystickUserPointer(int jid, void* p)
{
    if (jid >= 0 && jid <= GLFW_JOYSTICK_LAST) g_padUser[jid] = p;
}

API void* glfwGetJoystickUserPointer(int jid) { return jid >= 0 && jid <= GLFW_JOYSTICK_LAST ? g_padUser[jid] : nullptr; }
API int glfwJoystickIsGamepad(int jid) { return glfwJoystickPresent(jid); }

API GLFWjoystickfun glfwSetJoystickCallback(GLFWjoystickfun cb)
{
    auto old = g_joystickCb;
    g_joystickCb = cb;
    return old;
}

API int glfwUpdateGamepadMappings(const char*) { return GLFW_TRUE; }
API const char* glfwGetGamepadName(int jid) { return glfwGetJoystickName(jid); }
API int glfwGetGamepadState(int jid, GLFWgamepadstate* state) { return ReadPad(jid, state) ? GLFW_TRUE : GLFW_FALSE; }

// ---- clipboard (kept in the process, the real one needs the ui thread) ----
std::string g_clipboard;
API void glfwSetClipboardString(GLFWwindow*, const char* s) { g_clipboard = s ? s : ""; }
API const char* glfwGetClipboardString(GLFWwindow*) { return g_clipboard.c_str(); }

// ---- time ----
API double glfwGetTime(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart - g_base.QuadPart) / static_cast<double>(g_freq.QuadPart);
}

API void glfwSetTime(double t)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    g_base.QuadPart = now.QuadPart - static_cast<LONGLONG>(t * static_cast<double>(g_freq.QuadPart));
}

API uint64_t glfwGetTimerValue(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<uint64_t>(now.QuadPart);
}

API uint64_t glfwGetTimerFrequency(void) { return static_cast<uint64_t>(g_freq.QuadPart); }

// ---- context ----
API void glfwMakeContextCurrent(GLFWwindow* w)
{
    Window* win = W(w);
    if (!g_egl.MakeCurrent) {
        if (win) SetError(GLFW_NO_WINDOW_CONTEXT, "no egl yet");
        return;
    }
    EGLBoolean ok = win ? g_egl.MakeCurrent(g_egl.display, win->surface, win->surface, win->context)
                        : g_egl.MakeCurrent(g_egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (!ok) {
        SetError(GLFW_PLATFORM_ERROR, "eglMakeCurrent failed (0x%x)", g_egl.GetError());
        return;
    }
    t_current = win;
}

API GLFWwindow* glfwGetCurrentContext(void) { return reinterpret_cast<GLFWwindow*>(t_current); }

API void glfwSwapBuffers(GLFWwindow* w)
{
    if (W(w) == g_main) DrawPadCursor(g_main);
    auto t0 = perf::pclock::now();
    if (!g_egl.SwapBuffers(g_egl.display, W(w)->surface)) {
        SetError(GLFW_PLATFORM_ERROR, "eglSwapBuffers failed (0x%x)", g_egl.GetError());
    }
    if (W(w) == g_main) {
        using namespace perf;
        auto t1 = perf::pclock::now();
        if (g_lastSwapEnd.time_since_epoch().count()) {
            double cpu = std::chrono::duration<double, std::milli>(t0 - g_lastSwapEnd).count();
            double swap = std::chrono::duration<double, std::milli>(t1 - t0).count();
            g_cpuMs += cpu;
            g_swapMs += swap;
            g_worstMs = (std::max)(g_worstMs, cpu + swap);
            g_frames++;
        } else {
            g_reportStart = t1;
        }
        g_lastSwapEnd = t1;
        double span = std::chrono::duration<double>(t1 - g_reportStart).count();
        if (span >= 5.0 && g_frames > 0) {
            // draw calls that really reached the GLES driver (mobileglues counts them)
            using TakeDraws = unsigned (*)();
            static TakeDraws takeDraws = g_egl.mg ? reinterpret_cast<TakeDraws>(GetProcAddress(g_egl.mg, "mg_take_draw_count")) : nullptr;
            double draws = takeDraws ? static_cast<double>(takeDraws()) / g_frames : -1.0;
            Log("perf: %.1f fps, cpu %.2f ms, swap %.2f ms, worst frame %.1f ms, %.0f draws/frame", g_frames / span,
                g_cpuMs / g_frames, g_swapMs / g_frames, g_worstMs, draws);
            g_frames = 0;
            g_cpuMs = g_swapMs = g_worstMs = 0;
            g_reportStart = t1;
        }
    }
}

API void glfwSwapInterval(int interval)
{
    if (!t_current) {
        SetError(GLFW_NO_CURRENT_CONTEXT, "no current context");
        return;
    }
    g_egl.SwapInterval(g_egl.display, interval);
}

API GLFWglproc glfwGetProcAddress(const char* name)
{
    if (!t_current) {
        SetError(GLFW_NO_CURRENT_CONTEXT, "no current context");
        return nullptr;
    }
    return reinterpret_cast<GLFWglproc>(g_egl.GlProc(name));
}

API int glfwExtensionSupported(const char* name)
{
    if (!t_current || !name) return GLFW_FALSE;
    using GetIntegerv = void(__stdcall*)(unsigned, int*);
    using GetStringi = const unsigned char*(__stdcall*)(unsigned, unsigned);
    auto getInt = reinterpret_cast<GetIntegerv>(g_egl.GlProc("glGetIntegerv"));
    auto getStr = reinterpret_cast<GetStringi>(g_egl.GlProc("glGetStringi"));
    if (!getInt || !getStr) return GLFW_FALSE;
    int n = 0;
    getInt(0x821D, &n);  // GL_NUM_EXTENSIONS
    for (int i = 0; i < n; i++) {
        auto e = reinterpret_cast<const char*>(getStr(0x1F03, static_cast<unsigned>(i)));  // GL_EXTENSIONS
        if (e && strcmp(e, name) == 0) return GLFW_TRUE;
    }
    return GLFW_FALSE;
}

// ---- vulkan: not here ----
API int glfwVulkanSupported(void) { return GLFW_FALSE; }
API const char** glfwGetRequiredInstanceExtensions(uint32_t* count)
{
    if (count) *count = 0;
    return nullptr;
}
API void* glfwGetInstanceProcAddress(void*, const char*) { return nullptr; }
API int glfwGetPhysicalDevicePresentationSupport(void*, void*, uint32_t) { return GLFW_FALSE; }
API int glfwCreateWindowSurface(void*, GLFWwindow*, const void*, void*) { return -7; }  // VK_ERROR_EXTENSION_NOT_PRESENT

// ---- native access (lwjgl's GLFWNative* classes) ----
API const char* glfwGetWin32Adapter(GLFWmonitor*) { return nullptr; }
API const char* glfwGetWin32Monitor(GLFWmonitor*) { return nullptr; }
API void* glfwGetWin32Window(GLFWwindow*) { return nullptr; }
API GLFWwindow* glfwAttachWin32Window(void*, GLFWwindow*) { return nullptr; }
API void* glfwGetWGLContext(GLFWwindow*) { return nullptr; }
API EGLDisplay glfwGetEGLDisplay(void) { return g_egl.display; }
API EGLContext glfwGetEGLContext(GLFWwindow* w) { return W(w)->context; }
API EGLSurface glfwGetEGLSurface(GLFWwindow* w) { return W(w)->surface; }
