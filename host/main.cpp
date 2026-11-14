// step 2: desktop GL through mobileglues -> angle -> d3d11
// everything (egl + gl) goes through mobileglues.dll, the app doesnt touch angle itself.
// color: green + white triangle = works, blue = mobileglues.dll didnt load,
// magenta = it loaded but egl/gl failed. log goes to LocalState\step2.log,
// mobileglues writes its own to LocalState\MG\latest.log

#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>

#include <windows.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES2/gl2.h>

#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <share.h>
#include <string>

using namespace winrt;
using namespace winrt::Windows::ApplicationModel;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::UI::Core;

namespace {

// gl calls through mobileglues (types + enums come from the gles2 header,
// the functions themselves are looked up from mobileglues.dll)
struct Gl {
    const unsigned char* (__stdcall* GetString)(GLenum);
    GLenum (__stdcall* GetError)();
    void (__stdcall* Viewport)(GLint, GLint, GLsizei, GLsizei);
    void (__stdcall* ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (__stdcall* Clear)(GLbitfield);
    void (__stdcall* GenBuffers)(GLsizei, GLuint*);
    void (__stdcall* BindBuffer)(GLenum, GLuint);
    void (__stdcall* BufferData)(GLenum, GLsizeiptr, const void*, GLenum);
    GLuint (__stdcall* CreateShader)(GLenum);
    void (__stdcall* ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
    void (__stdcall* CompileShader)(GLuint);
    void (__stdcall* GetShaderiv)(GLuint, GLenum, GLint*);
    void (__stdcall* GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    GLuint (__stdcall* CreateProgram)();
    void (__stdcall* AttachShader)(GLuint, GLuint);
    void (__stdcall* BindAttribLocation)(GLuint, GLuint, const GLchar*);
    void (__stdcall* LinkProgram)(GLuint);
    void (__stdcall* GetProgramiv)(GLuint, GLenum, GLint*);
    void (__stdcall* GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    void (__stdcall* UseProgram)(GLuint);
    void (__stdcall* EnableVertexAttribArray)(GLuint);
    void (__stdcall* VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
    void (__stdcall* DrawArrays)(GLenum, GLint, GLsizei);
};

struct Egl {
    PFNEGLGETERRORPROC GetError;
    PFNEGLGETPLATFORMDISPLAYEXTPROC GetPlatformDisplayEXT;
    PFNEGLINITIALIZEPROC Initialize;
    PFNEGLQUERYSTRINGPROC QueryString;
    PFNEGLBINDAPIPROC BindAPI;
    PFNEGLCHOOSECONFIGPROC ChooseConfig;
    PFNEGLCREATEWINDOWSURFACEPROC CreateWindowSurface;
    PFNEGLCREATECONTEXTPROC CreateContext;
    PFNEGLMAKECURRENTPROC MakeCurrent;
    PFNEGLSWAPBUFFERSPROC SwapBuffers;
    PFNEGLDESTROYCONTEXTPROC DestroyContext;
    PFNEGLDESTROYSURFACEPROC DestroySurface;
    PFNEGLTERMINATEPROC Terminate;
};

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

std::wstring LocalDir()
{
    return std::wstring{ Windows::Storage::ApplicationData::Current().LocalFolder().Path() };
}

std::string Narrow(std::wstring const& w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

void OpenLog()
{
    std::wstring path = LocalDir() + L"\\step2.log";
    // shared so device portal can read it while the app runs
    g_log = _wfsopen(path.c_str(), L"w", _SH_DENYNO);
    LogLine("xbox-mc step 2 test");
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

const char* Str(const unsigned char* s) { return s ? reinterpret_cast<const char*>(s) : "(null)"; }

const char kVertexShader[] =
    "#version 120\n"
    "attribute vec2 pos;\n"
    "void main() { gl_Position = vec4(pos, 0.0, 1.0); }\n";

const char kFragmentShader[] =
    "#version 120\n"
    "void main() { gl_FragColor = vec4(1.0); }\n";

struct Renderer {
    HMODULE mg = nullptr;
    Egl egl{};
    Gl gl{};
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    GLuint program = 0;

    template <typename T> bool Get(T& fn, const char* name)
    {
        fn = reinterpret_cast<T>(GetProcAddress(mg, name));
        if (!fn) LogLine("missing export: %s", name);
        return fn != nullptr;
    }

    template <typename T> bool GetGl(T& fn, const char* name)
    {
        using Lookup = void* (__stdcall*)(const char*);
        static Lookup lookup = reinterpret_cast<Lookup>(GetProcAddress(mg, "wglGetProcAddress"));
        void* p = lookup ? lookup(name) : nullptr;
        if (!p) p = reinterpret_cast<void*>(GetProcAddress(mg, name));
        fn = reinterpret_cast<T>(p);
        if (!fn) LogLine("missing gl function: %s", name);
        return fn != nullptr;
    }

    bool Load()
    {
        // mobileglues reads this with getenv while it loads
        std::string mgDir = Narrow(LocalDir() + L"\\MG");
        CreateDirectoryA(mgDir.c_str(), nullptr);
        _putenv_s("MG_DIR_PATH", mgDir.c_str());
        LogLine("MG_DIR_PATH = %s", mgDir.c_str());

        auto t0 = std::chrono::steady_clock::now();
        mg = LoadPackagedLibrary(L"mobileglues.dll", 0);
        if (!mg) {
            LogLine("LoadPackagedLibrary(mobileglues.dll) failed, error %lu", GetLastError());
            return false;
        }
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        LogLine("mobileglues.dll loaded in %.0f ms", ms);
        return true;
    }

    bool InitEgl(CoreWindow const& window)
    {
        bool ok = Get(egl.GetError, "eglGetError") && Get(egl.GetPlatformDisplayEXT, "eglGetPlatformDisplayEXT") &&
                  Get(egl.Initialize, "eglInitialize") && Get(egl.QueryString, "eglQueryString") &&
                  Get(egl.BindAPI, "eglBindAPI") && Get(egl.ChooseConfig, "eglChooseConfig") &&
                  Get(egl.CreateWindowSurface, "eglCreateWindowSurface") &&
                  Get(egl.CreateContext, "eglCreateContext") && Get(egl.MakeCurrent, "eglMakeCurrent") &&
                  Get(egl.SwapBuffers, "eglSwapBuffers") && Get(egl.DestroyContext, "eglDestroyContext") &&
                  Get(egl.DestroySurface, "eglDestroySurface") && Get(egl.Terminate, "eglTerminate");
        if (!ok) return false;

        const EGLint displayAttribs[] = {
            EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
#ifdef EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE
            // needed for suspend on uwp
            EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE, EGL_TRUE,
#endif
            EGL_NONE,
        };
        display = egl.GetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, displayAttribs);
        EGLint major = 0, minor = 0;
        if (display == EGL_NO_DISPLAY || !egl.Initialize(display, &major, &minor)) {
            LogLine("eglInitialize failed: %s", EglErrorName(egl.GetError()));
            return false;
        }
        LogLine("EGL %d.%d, vendor: %s", major, minor, egl.QueryString(display, EGL_VENDOR));

        // desktop gl, mobileglues turns this into gles underneath
        if (!egl.BindAPI(EGL_OPENGL_API)) {
            LogLine("eglBindAPI(EGL_OPENGL_API) failed: %s", EglErrorName(egl.GetError()));
            return false;
        }

        const EGLint configAttribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
            EGL_NONE,
        };
        EGLConfig config = nullptr;
        EGLint count = 0;
        if (!egl.ChooseConfig(display, configAttribs, &config, 1, &count) || count < 1) {
            LogLine("eglChooseConfig failed: %s (count %d)", EglErrorName(egl.GetError()), count);
            return false;
        }

        // angle takes the CoreWindow directly
        auto native = reinterpret_cast<EGLNativeWindowType>(get_abi(window));
        surface = egl.CreateWindowSurface(display, config, native, nullptr);
        if (surface == EGL_NO_SURFACE) {
            LogLine("eglCreateWindowSurface failed: %s", EglErrorName(egl.GetError()));
            return false;
        }

        // no version asked, same as what minecraft would get
        context = egl.CreateContext(display, config, EGL_NO_CONTEXT, nullptr);
        if (context == EGL_NO_CONTEXT) {
            LogLine("eglCreateContext failed: %s", EglErrorName(egl.GetError()));
            return false;
        }
        if (!egl.MakeCurrent(display, surface, surface, context)) {
            LogLine("eglMakeCurrent failed: %s", EglErrorName(egl.GetError()));
            return false;
        }
        LogLine("EGL setup done");
        return true;
    }

    bool CheckGl(const char* where)
    {
        GLenum e = gl.GetError();
        if (e == GL_NO_ERROR) return true;
        LogLine("GL error 0x%04x after %s", e, where);
        return false;
    }

    GLuint Compile(GLenum type, const char* src)
    {
        GLuint s = gl.CreateShader(type);
        gl.ShaderSource(s, 1, &src, nullptr);
        gl.CompileShader(s);
        GLint ok = 0;
        gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char info[1024] = {};
            gl.GetShaderInfoLog(s, sizeof(info), nullptr, info);
            LogLine("%s shader failed: %s", type == GL_VERTEX_SHADER ? "vertex" : "fragment", info);
            return 0;
        }
        return s;
    }

    bool InitGl()
    {
        bool ok = GetGl(gl.GetString, "glGetString") && GetGl(gl.GetError, "glGetError") &&
                  GetGl(gl.Viewport, "glViewport") && GetGl(gl.ClearColor, "glClearColor") &&
                  GetGl(gl.Clear, "glClear") && GetGl(gl.GenBuffers, "glGenBuffers") &&
                  GetGl(gl.BindBuffer, "glBindBuffer") && GetGl(gl.BufferData, "glBufferData") &&
                  GetGl(gl.CreateShader, "glCreateShader") && GetGl(gl.ShaderSource, "glShaderSource") &&
                  GetGl(gl.CompileShader, "glCompileShader") && GetGl(gl.GetShaderiv, "glGetShaderiv") &&
                  GetGl(gl.GetShaderInfoLog, "glGetShaderInfoLog") && GetGl(gl.CreateProgram, "glCreateProgram") &&
                  GetGl(gl.AttachShader, "glAttachShader") &&
                  GetGl(gl.BindAttribLocation, "glBindAttribLocation") && GetGl(gl.LinkProgram, "glLinkProgram") &&
                  GetGl(gl.GetProgramiv, "glGetProgramiv") && GetGl(gl.GetProgramInfoLog, "glGetProgramInfoLog") &&
                  GetGl(gl.UseProgram, "glUseProgram") &&
                  GetGl(gl.EnableVertexAttribArray, "glEnableVertexAttribArray") &&
                  GetGl(gl.VertexAttribPointer, "glVertexAttribPointer") && GetGl(gl.DrawArrays, "glDrawArrays");
        if (!ok) return false;

        LogLine("GL_VERSION: %s", Str(gl.GetString(GL_VERSION)));
        LogLine("GL_RENDERER: %s", Str(gl.GetString(GL_RENDERER)));
        LogLine("GL_VENDOR: %s", Str(gl.GetString(GL_VENDOR)));
        LogLine("GL_SHADING_LANGUAGE_VERSION: %s", Str(gl.GetString(GL_SHADING_LANGUAGE_VERSION)));

        // gl 2.1 style, like minecraft 1.16.5: vbo + #version 120
        GLuint vs = Compile(GL_VERTEX_SHADER, kVertexShader);
        GLuint fs = Compile(GL_FRAGMENT_SHADER, kFragmentShader);
        if (!vs || !fs) return false;
        program = gl.CreateProgram();
        gl.AttachShader(program, vs);
        gl.AttachShader(program, fs);
        gl.BindAttribLocation(program, 0, "pos");
        gl.LinkProgram(program);
        GLint linked = 0;
        gl.GetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            char info[1024] = {};
            gl.GetProgramInfoLog(program, sizeof(info), nullptr, info);
            LogLine("link failed: %s", info);
            return false;
        }

        const GLfloat verts[] = { 0.0f, 0.6f, -0.6f, -0.5f, 0.6f, -0.5f };
        GLuint vbo = 0;
        gl.GenBuffers(1, &vbo);
        gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
        gl.BufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        gl.EnableVertexAttribArray(0);
        gl.VertexAttribPointer(0, 2, GL_FLOAT, 0, 0, nullptr);
        if (!CheckGl("setup")) return false;
        LogLine("GL setup done");
        return true;
    }

    void Shutdown()
    {
        if (display == EGL_NO_DISPLAY) return;
        egl.MakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context != EGL_NO_CONTEXT) egl.DestroyContext(display, context);
        if (surface != EGL_NO_SURFACE) egl.DestroySurface(display, surface);
        egl.Terminate(display);
        display = EGL_NO_DISPLAY;
    }
};

struct App : implements<App, IFrameworkViewSource, IFrameworkView> {
    CoreWindow m_window{ nullptr };
    bool m_closed = false;
    Renderer m_r;

    IFrameworkView CreateView() { return *this; }

    void Initialize(CoreApplicationView const&) { OpenLog(); }

    void SetWindow(CoreWindow const& window)
    {
        m_window = window;
        window.Closed([this](auto&&, auto&&) { m_closed = true; });
    }

    void Load(hstring const&) {}

    void Uninitialize() { m_r.Shutdown(); }

    // when mobileglues cant give us a context, paint the result color with
    // angle directly (linked into the exe) so theres still something to see
    void Fallback(float r, float g, float b)
    {
        m_r.Shutdown();
        EGLDisplay dpy = EGL_NO_DISPLAY;
        EGLSurface surf = EGL_NO_SURFACE;
        auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
        const EGLint displayAttribs[] = {
            EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE, EGL_NONE,
        };
        const EGLint configAttribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE,
        };
        const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        EGLConfig config = nullptr;
        EGLint count = 0;
        bool ok = getPlatformDisplay &&
                  (dpy = getPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, displayAttribs)) !=
                      EGL_NO_DISPLAY &&
                  eglInitialize(dpy, nullptr, nullptr) && eglBindAPI(EGL_OPENGL_ES_API) &&
                  eglChooseConfig(dpy, configAttribs, &config, 1, &count) && count > 0;
        if (ok) {
            surf = eglCreateWindowSurface(dpy, config, reinterpret_cast<EGLNativeWindowType>(get_abi(m_window)), nullptr);
            EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, contextAttribs);
            ok = surf != EGL_NO_SURFACE && ctx != EGL_NO_CONTEXT && eglMakeCurrent(dpy, surf, surf, ctx);
        }
        if (!ok) LogLine("fallback angle context failed too: %s", EglErrorName(eglGetError()));

        auto dispatcher = m_window.Dispatcher();
        while (!m_closed) {
            if (!ok) {
                dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
                continue;
            }
            dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
            glClearColor(r, g, b, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            eglSwapBuffers(dpy, surf);
        }
    }

    void Run()
    {
        m_window.Activate();

        uint64_t limit = Windows::System::MemoryManager::AppMemoryUsageLimit();
        LogLine("AppMemoryUsageLimit: %llu bytes (%.2f GB)", limit,
                static_cast<double>(limit) / (1024.0 * 1024.0 * 1024.0));
        auto bounds = m_window.Bounds();
        LogLine("CoreWindow bounds: %.0f x %.0f", bounds.Width, bounds.Height);

        bool loaded = m_r.Load();
        bool eglOk = loaded && m_r.InitEgl(m_window);
        bool glOk = eglOk && m_r.InitGl();

        if (!loaded) {
            LogLine("result: blue");
            Fallback(0.1f, 0.3f, 0.9f);
            return;
        }
        if (!eglOk) {
            LogLine("result: magenta (egl failed)");
            Fallback(0.8f, 0.0f, 0.8f);
            return;
        }

        float r, g, b;
        if (glOk) { r = 0.0f; g = 0.7f; b = 0.2f; LogLine("result: green"); }
        else      { r = 0.8f; g = 0.0f; b = 0.8f; LogLine("result: magenta (gl failed)"); }

        auto& gl = m_r.gl;
        auto start = std::chrono::steady_clock::now();
        int frames = 0;
        auto dispatcher = m_window.Dispatcher();
        while (!m_closed) {
            dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);

            double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            float pulse = 0.75f + 0.25f * static_cast<float>(std::sin(t * 2.0));
            auto bb = m_window.Bounds();
            gl.Viewport(0, 0, static_cast<GLsizei>(bb.Width), static_cast<GLsizei>(bb.Height));
            gl.ClearColor(r * pulse, g * pulse, b * pulse, 1.0f);
            gl.Clear(GL_COLOR_BUFFER_BIT);
            if (glOk) {
                gl.UseProgram(m_r.program);
                gl.DrawArrays(GL_TRIANGLES, 0, 3);
                if (frames == 0 && !m_r.CheckGl("first draw")) {
                    glOk = false;
                    r = 0.8f; g = 0.0f; b = 0.8f;
                    LogLine("result: magenta (draw failed)");
                }
            }
            if (!m_r.egl.SwapBuffers(m_r.display, m_r.surface)) {
                LogLine("eglSwapBuffers failed: %s", EglErrorName(m_r.egl.GetError()));
                break;
            }

            if (++frames == 600) LogLine("600 frames in %.2f s (%.1f fps)", t, frames / t);
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
