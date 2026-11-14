// step 1: CoreWindow + ANGLE, just clears the screen
// color: green = GLES3 + game mode, yellow = GLES3 but app mode,
// orange = GLES2 only, red = EGL failed. log goes to LocalState\step1.log

#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES3/gl3.h>

#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>

using namespace winrt;
using namespace winrt::Windows::ApplicationModel;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::UI::Core;

namespace {

FILE* g_log = nullptr;

void LogLine(const char* fmt, ...)
{
    if (!g_log) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    va_end(args);
    fputc('\n', g_log);
    fflush(g_log);
}

void OpenLog()
{
    std::wstring dir{ Windows::Storage::ApplicationData::Current().LocalFolder().Path() };
    std::wstring path = dir + L"\\step1.log";
    _wfopen_s(&g_log, path.c_str(), L"w");
    LogLine("xbox-mc step 1 test");
}

const char* EglErrorName(EGLint e)
{
    switch (e) {
    case EGL_SUCCESS: return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED: return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS: return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC: return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE: return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONFIG: return "EGL_BAD_CONFIG";
    case EGL_BAD_CONTEXT: return "EGL_BAD_CONTEXT";
    case EGL_BAD_DISPLAY: return "EGL_BAD_DISPLAY";
    case EGL_BAD_MATCH: return "EGL_BAD_MATCH";
    case EGL_BAD_NATIVE_WINDOW: return "EGL_BAD_NATIVE_WINDOW";
    case EGL_BAD_PARAMETER: return "EGL_BAD_PARAMETER";
    case EGL_BAD_SURFACE: return "EGL_BAD_SURFACE";
    default: return "other";
    }
}

struct Gles {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int esMajor = 0;

    bool Init(CoreWindow const& window)
    {
        auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
        if (!getPlatformDisplay) {
            LogLine("eglGetPlatformDisplayEXT missing");
            return false;
        }

        const EGLint displayAttribs[] = {
            EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
#ifdef EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE
            // needed for suspend on uwp
            EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE, EGL_TRUE,
#endif
            EGL_NONE,
        };
        display = getPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, displayAttribs);
        EGLint major = 0, minor = 0;
        if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor)) {
            LogLine("eglInitialize failed: %s", EglErrorName(eglGetError()));
            return false;
        }
        LogLine("EGL %d.%d, vendor: %s", major, minor, eglQueryString(display, EGL_VENDOR));
        LogLine("EGL version: %s", eglQueryString(display, EGL_VERSION));

        // try gles3, fall back to 2
        for (int want : { 3, 2 }) {
            const EGLint configAttribs[] = {
                EGL_RENDERABLE_TYPE, want == 3 ? EGL_OPENGL_ES3_BIT_KHR : EGL_OPENGL_ES2_BIT,
                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
                EGL_NONE,
            };
            EGLConfig config = nullptr;
            EGLint count = 0;
            if (!eglChooseConfig(display, configAttribs, &config, 1, &count) || count < 1) {
                LogLine("no ES %d config: %s", want, EglErrorName(eglGetError()));
                continue;
            }

            if (surface == EGL_NO_SURFACE) {
                // angle takes the CoreWindow directly
                auto native = reinterpret_cast<EGLNativeWindowType>(get_abi(window));
                surface = eglCreateWindowSurface(display, config, native, nullptr);
                if (surface == EGL_NO_SURFACE) {
                    LogLine("eglCreateWindowSurface failed: %s", EglErrorName(eglGetError()));
                    return false;
                }
            }

            const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, want, EGL_NONE };
            context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
            if (context == EGL_NO_CONTEXT) {
                LogLine("ES %d context failed: %s", want, EglErrorName(eglGetError()));
                continue;
            }
            if (!eglMakeCurrent(display, surface, surface, context)) {
                LogLine("eglMakeCurrent failed: %s", EglErrorName(eglGetError()));
                return false;
            }
            esMajor = want;
            break;
        }
        if (esMajor == 0) return false;

        LogLine("GL_VERSION: %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
        LogLine("GL_RENDERER: %s", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
        LogLine("GL_SHADING_LANGUAGE_VERSION: %s",
                reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)));
        GLint maxTex = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
        LogLine("GL_MAX_TEXTURE_SIZE: %d", maxTex);
        return true;
    }

    void Shutdown()
    {
        if (display == EGL_NO_DISPLAY) return;
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
        if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
        eglTerminate(display);
        display = EGL_NO_DISPLAY;
        surface = EGL_NO_SURFACE;
        context = EGL_NO_CONTEXT;
    }
};

struct App : implements<App, IFrameworkViewSource, IFrameworkView> {
    CoreWindow m_window{ nullptr };
    bool m_closed = false;
    Gles m_gles;

    IFrameworkView CreateView() { return *this; }

    void Initialize(CoreApplicationView const&)
    {
        OpenLog();
    }

    void SetWindow(CoreWindow const& window)
    {
        m_window = window;
        window.Closed([this](auto&&, auto&&) { m_closed = true; });
    }

    void Load(hstring const&) {}

    void Uninitialize() { m_gles.Shutdown(); }

    void Run()
    {
        m_window.Activate();

        uint64_t limit = Windows::System::MemoryManager::AppMemoryUsageLimit();
        double limitGb = static_cast<double>(limit) / (1024.0 * 1024.0 * 1024.0);
        LogLine("AppMemoryUsageLimit: %llu bytes (%.2f GB)", limit, limitGb);
        auto bounds = m_window.Bounds();
        LogLine("CoreWindow bounds: %.0f x %.0f", bounds.Width, bounds.Height);

        bool ok = m_gles.Init(m_window);
        LogLine(ok ? "EGL setup done" : "EGL setup failed");

        float r, g, b;
        if (!ok)                  { r = 0.8f; g = 0.0f; b = 0.0f; }
        else if (m_gles.esMajor < 3) { r = 1.0f; g = 0.5f; b = 0.0f; }
        else if (limitGb < 4.0)   { r = 0.9f; g = 0.8f; b = 0.0f; }
        else                      { r = 0.0f; g = 0.7f; b = 0.2f; }

        auto start = std::chrono::steady_clock::now();
        int frames = 0;
        auto dispatcher = m_window.Dispatcher();
        while (!m_closed) {
            dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
            if (!ok) {
                // cant draw, just keep the red window open
                dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
                continue;
            }

            double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            float pulse = 0.75f + 0.25f * static_cast<float>(std::sin(t * 2.0));
            glClearColor(r * pulse, g * pulse, b * pulse, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            if (!eglSwapBuffers(m_gles.display, m_gles.surface)) {
                LogLine("eglSwapBuffers failed: %s", EglErrorName(eglGetError()));
                break;
            }

            if (++frames == 600) {
                LogLine("600 frames in %.2f s (%.1f fps)", t, frames / t);
            }
        }
    }
};

} // namespace

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    init_apartment();
    CoreApplication::Run(make<App>());
    return 0;
}
