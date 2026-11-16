// step 5: the launcher from java (login + minecraft download). the host only
// owns the CoreWindow and the jvm, java draws through my glfw3.dll (CoreWindow +
// EGL via mobileglues). if java never gets a window up the host paints:
// red = jvm.dll didnt load, orange = jvm or the launcher failed,
// blue = glfw3.dll didnt load.
// logs in LocalState: host.log (this + jvm output), launcher.log, glfw.log,
// MG\latest.log (mobileglues), hs_err_pid*.log if the jvm crashes

#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Networking.h>
#include <winrt/Windows.Networking.Connectivity.h>
#include <winrt/Windows.Networking.Sockets.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>

#include <windows.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES2/gl2.h>
#include <jni.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <share.h>
#include <string>
#include <thread>
#include <vector>

using namespace winrt;
using namespace winrt::Windows::ApplicationModel;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::UI::Core;

namespace {

const char* kMainClass = "Launcher";

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

std::wstring InstallDir()
{
    return std::wstring{ Package::Current().InstalledLocation().Path() };
}

std::string Narrow(std::wstring const& w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

#include "logserver.h"
#include "shadertest.h"

void OpenLog()
{
    std::wstring path = LocalDir() + L"\\host.log";
    // keep the last run's logs (a crash that kills the whole app leaves no other trace)
    for (auto name : { L"host", L"launcher" }) {
        std::wstring cur = LocalDir() + L"\\" + name + L".log";
        std::wstring prev = LocalDir() + L"\\" + name + L".prev.log";
        DeleteFileW(prev.c_str());
        MoveFileExW(cur.c_str(), prev.c_str(), 0);
    }
    // shared so device portal can read it while the app runs
    g_log = _wfsopen(path.c_str(), L"w", _SH_DENYNO);
    LogLine("xbox-mc host");

    // memory use every 10 s, the xbox kills apps that go over their limit
    std::thread([] {
        for (;;) {
            Sleep(10000);
            uint64_t used = Windows::System::MemoryManager::AppMemoryUsage();
            uint64_t limit = Windows::System::MemoryManager::AppMemoryUsageLimit();
            LogLine("mem: %llu MB of %llu MB", used >> 20, limit >> 20);
        }
    }).detach();
}

// ---- glfw3.dll ----

using AttachFn = void (*)(void*);
using HasSurfaceFn = int (*)();
HasSurfaceFn g_glfwHasSurface = nullptr;

bool AttachGlfw(CoreWindow const& window)
{
    HMODULE glfw = LoadPackagedLibrary(L"glfw3.dll", 0);
    if (!glfw) {
        LogLine("LoadPackagedLibrary(glfw3.dll) failed, error %lu", GetLastError());
        return false;
    }
    auto attach = reinterpret_cast<AttachFn>(GetProcAddress(glfw, "xmcglfw_attach"));
    g_glfwHasSurface = reinterpret_cast<HasSurfaceFn>(GetProcAddress(glfw, "xmcglfw_has_surface"));
    if (!attach || !g_glfwHasSurface) {
        LogLine("glfw3.dll is missing xmcglfw_attach / xmcglfw_has_surface");
        return false;
    }
    attach(get_abi(window));
    LogLine("glfw3.dll attached to the CoreWindow");
    return true;
}

// ---- java ----

enum class JavaState { Starting, Running, Done, NoJvmDll, Failed };
std::atomic<JavaState> g_java{ JavaState::Starting };
CoreDispatcher g_dispatcher{ nullptr };

void SetJava(JavaState s)
{
    g_java = s;
    // wake the ui loop so it can react
    if (g_dispatcher) g_dispatcher.RunAsync(CoreDispatcherPriority::Normal, [] {});
}

// LoadLibraryExW isnt in the uwp headers. it still exists, so find it in
// kernelbase through the module that holds a function I can link
using LoadLibraryExWFn = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
LoadLibraryExWFn FindLoadLibraryExW()
{
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<void*>(&CreateFile2), &mbi, sizeof(mbi))) return nullptr;
    return reinterpret_cast<LoadLibraryExWFn>(
        GetProcAddress(static_cast<HMODULE>(mbi.AllocationBase), "LoadLibraryExW"));
}

// the system dlls jvm.dll + the core java dlls import. not all of them
// have to exist on the xbox, this says which ones dont
void ProbeSystemDlls()
{
    auto loadEx = FindLoadLibraryExW();
    if (!loadEx) {
        LogLine("probe: LoadLibraryExW not found");
        return;
    }
    const wchar_t* dlls[] = { L"kernel32.dll", L"user32.dll", L"advapi32.dll", L"powrprof.dll", L"ws2_32.dll",
                              L"winmm.dll", L"version.dll", L"ucrtbase.dll", L"shell32.dll", L"comctl32.dll",
                              L"winhttp.dll", L"secur32.dll", L"iphlpapi.dll", L"mswsock.dll" };
    for (auto name : dlls) {
        HMODULE h = loadEx(name, nullptr, 0);
        if (h) LogLine("probe: %ls ok", name);
        else LogLine("probe: %ls MISSING (error %lu)", name, GetLastError());
    }
}

jint JNICALL JvmVfprintf(FILE*, const char* fmt, va_list args)
{
    if (!g_log) return 0;
    int n = vfprintf(g_log, fmt, args);
    fflush(g_log);
    return n;
}

// when java crashes (exit code != 0, or abort) the process would just vanish,
// and the log server with it. so the calling thread waits here forever and the
// app stays open until it gets closed. a normal quit (exit 0) still closes it.
void JNICALL JvmExit(jint code)
{
    LogLine("jvm: exit(%d)", code);
    if (code != 0) {
        LogLine("java crashed, keeping the app open so the logs can be read");
        Sleep(INFINITE);
    }
}

void JNICALL JvmAbort()
{
    LogLine("jvm: abort, keeping the app open so the logs can be read");
    Sleep(INFINITE);
}

std::string ExceptionText(JNIEnv* env)
{
    jthrowable ex = env->ExceptionOccurred();
    if (!ex) return "";
    env->ExceptionClear();
    std::string text = "(no message)";
    jclass cls = env->GetObjectClass(ex);
    if (jmethodID toString = env->GetMethodID(cls, "toString", "()Ljava/lang/String;")) {
        if (auto s = static_cast<jstring>(env->CallObjectMethod(ex, toString))) {
            const char* c = env->GetStringUTFChars(s, nullptr);
            text = c;
            env->ReleaseStringUTFChars(s, c);
        }
    }
    // full trace goes to java.log if the test class got to setErr
    if (jmethodID print = env->GetMethodID(cls, "printStackTrace", "()V")) env->CallVoidMethod(ex, print);
    env->ExceptionClear();
    return text;
}

// which java + lwjgl this start uses. the launcher writes LocalState\runtime.txt
// when a different minecraft version gets picked (line 1: 17 or 21, line 2: the
// game folder) and then restarts the app, since one process only gets one jvm.
// 17 = minecraft 1.17.1 (java 17, lwjgl 3.2.2), 21 = 1.21.11 (java 21, lwjgl 3.3.3)
struct JavaRuntime {
    std::string java = "17";
    std::string gameFolder = "game";
    std::wstring jre = L"jre";
    std::string patch = "patch", lib = "lib", natives = "natives", lwjgl = "3.2.2";
};

JavaRuntime ReadRuntime()
{
    JavaRuntime r;
    FILE* f = nullptr;
    if (_wfopen_s(&f, (LocalDir() + L"\\runtime.txt").c_str(), L"r") == 0 && f) {
        char line[256];
        for (int i = 0; i < 2 && fgets(line, sizeof(line), f); i++) {
            std::string l = line;
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r' || l.back() == ' ')) l.pop_back();
            if (i == 0 && l == "21") r.java = "21";
            // a plain folder name inside LocalState, nothing else
            if (i == 1 && !l.empty() && l.find_first_of("\\/:") == std::string::npos && l.find("..") == std::string::npos)
                r.gameFolder = l;
        }
        fclose(f);
    }
    if (r.java == "21") {
        r.jre = L"jre21";
        r.patch = "patch21";
        r.lib = "lib21";
        r.natives = "natives21";
        r.lwjgl = "3.3.3";
    } else {
        r.gameFolder = "game";
    }
    return r;
}

// the launcher drops LocalState\restart.now after switching versions. restarting
// is a winrt call, so the host does it. if windows says no, restart.failed tells
// the launcher to ask for a manual restart instead
DWORD WINAPI RestartWatcher(void*)
{
    std::wstring flag = LocalDir() + L"\\restart.now";
    while (true) {
        Sleep(250);
        if (GetFileAttributesW(flag.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        DeleteFileW(flag.c_str());
        LogLine("restarting for the other minecraft version");
        AppRestartFailureReason why = AppRestartFailureReason::Other;
        try {
            why = CoreApplication::RequestRestartAsync(L"").get();
        } catch (...) {
        }
        LogLine("restart request failed (%d)", static_cast<int>(why));
        FILE* f = nullptr;
        if (_wfopen_s(&f, (LocalDir() + L"\\restart.failed").c_str(), L"w") == 0 && f) fclose(f);
    }
}

void RunJava()
{
    ProbeSystemDlls();
    DeleteFileW((LocalDir() + L"\\restart.failed").c_str());
    if (HANDLE t = CreateThread(nullptr, 0, RestartWatcher, nullptr, 0, nullptr)) CloseHandle(t);

    auto t0 = std::chrono::steady_clock::now();
    auto elapsed = [&] {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    };

    JavaRuntime rt = ReadRuntime();
    LogLine("runtime: java %s, lwjgl %s, folder %s", rt.java.c_str(), rt.lwjgl.c_str(), rt.gameFolder.c_str());
    std::wstring jvmPath = rt.jre + L"\\bin\\server\\jvm.dll";
    HMODULE jvm = LoadPackagedLibrary(jvmPath.c_str(), 0);
    if (!jvm) {
        LogLine("LoadPackagedLibrary(%ls) failed, error %lu", jvmPath.c_str(), GetLastError());
        SetJava(JavaState::NoJvmDll);
        return;
    }
    LogLine("jvm.dll loaded in %.0f ms", elapsed());

    using CreateVm = jint(JNICALL*)(JavaVM**, void**, void*);
    auto create = reinterpret_cast<CreateVm>(GetProcAddress(jvm, "JNI_CreateJavaVM"));
    if (!create) {
        LogLine("JNI_CreateJavaVM not exported");
        SetJava(JavaState::Failed);
        return;
    }

    std::string local = Narrow(LocalDir());
    std::string install = Narrow(InstallDir());
    std::string tmp = local + "\\tmp";
    CreateDirectoryA(tmp.c_str(), nullptr);

    // minecraft (log4j, options.txt...) writes relative to the current folder,
    // which starts as the read-only package folder. SetCurrentDirectoryW isn't in
    // the uwp headers either, same trick as LoadLibraryExW
    std::string gameDir = local + "\\" + rt.gameFolder;
    CreateDirectoryA(gameDir.c_str(), nullptr);
    {
        MEMORY_BASIC_INFORMATION mbi{};
        using SetCwdFn = BOOL(WINAPI*)(LPCWSTR);
        SetCwdFn setCwd = nullptr;
        if (VirtualQuery(reinterpret_cast<void*>(&CreateFile2), &mbi, sizeof(mbi)))
            setCwd = reinterpret_cast<SetCwdFn>(GetProcAddress(static_cast<HMODULE>(mbi.AllocationBase), "SetCurrentDirectoryW"));
        std::wstring wgame = LocalDir() + L"\\" + std::wstring(rt.gameFolder.begin(), rt.gameFolder.end());
        if (setCwd && setCwd(wgame.c_str())) LogLine("current folder: %s", gameDir.c_str());
        else LogLine("couldn't set the current folder (error %lu)", GetLastError());
    }

    std::string cp = install + "\\java";
    for (auto jar : { "lwjgl", "lwjgl-glfw", "lwjgl-opengl" })
        cp += ";" + install + "\\" + rt.lib + "\\" + jar + "-" + rt.lwjgl + ".jar";

    std::string opts[] = {
        "-Djava.class.path=" + cp,
        "-Duser.home=" + local,
        // so java can find client_id.txt in the package
        "-Dxmc.install=" + install,
        // which runtime this is, the launcher restarts the app if the picked
        // minecraft version needs the other one
        "-Dxmc.runtime=" + rt.java,
        // my jdk fixes for the sandbox (java\jdk-patches)
        "--patch-module=java.base=" + install + "\\" + rt.patch + "\\java.base",
        "-Djava.io.tmpdir=" + tmp,
        // java resolves relative paths (config\..., logs\...) against user.dir,
        // which it only reads at startup. the package folder is read-only
        "-Duser.dir=" + gameDir,
        // no awt windows in a uwp app, fabric shows its errors in the log then
        "-Djava.awt.headless=true",
        // jna (narrator, system info): its dll ships in natives\, a uwp app
        // can't load one it unpacked to temp
        "-Djna.tmpdir=" + tmp,
        "-Djna.boot.library.path=" + install + "\\" + rt.natives,
        "-Djna.nounpack=true",
        // 2 gb heap max: the xbox kills the app over 5 gb in total, and the jvm,
        // mobileglues, angle and textures need room next to the heap
        "-Xmx2048m",
        "-XX:+DisableExplicitGC",
        // no shared memory perf file, it wants a desktop temp dir
        "-XX:-UsePerfData",
        "-XX:ErrorFile=" + local + "\\hs_err_pid%p.log",
        // jvm log output ends up in the vfprintf hook below, so in host.log
        // gc=info: every gc pause shows up in host.log (hitch hunting)
        "-Xlog:all=warning,os=info,gc=info",
        // lwjgl: natives from the package, my glfw, gl through mobileglues
        "-Dorg.lwjgl.librarypath=" + install + "\\" + rt.natives,
        "-Dorg.lwjgl.glfw.libname=" + install + "\\glfw3.dll",
        "-Dorg.lwjgl.opengl.libname=" + install + "\\mobileglues.dll",
        "-Dorg.lwjgl.util.Debug=true",
        "-Dorg.lwjgl.util.DebugLoader=true",
    };
    std::vector<std::string> extra;
    // gc flags. LocalState\jvm.txt (one flag per line) replaces the defaults,
    // so gc setups can be compared without a new build
    std::vector<std::string> gcFlags = { "-Xms1024m", "-XX:+UseG1GC", "-XX:MaxGCPauseMillis=30",
                                         "-XX:+ParallelRefProcEnabled" };
    {
        FILE* jf = nullptr;
        if (_wfopen_s(&jf, (LocalDir() + L"\\jvm.txt").c_str(), L"r") == 0 && jf) {
            gcFlags.clear();
            char line[512];
            while (fgets(line, sizeof(line), jf)) {
                std::string l = line;
                while (!l.empty() && (l.back() == '\n' || l.back() == '\r' || l.back() == ' ')) l.pop_back();
                if (!l.empty() && l[0] == '-') gcFlags.push_back(l);
            }
            fclose(jf);
            LogLine("gc flags from jvm.txt");
        }
    }
    extra.insert(extra.end(), gcFlags.begin(), gcFlags.end());
    // profiling: with LocalState\jfr.on a 60 s flight recording starts 90 s in
    if (GetFileAttributesW((LocalDir() + L"\\jfr.on").c_str()) != INVALID_FILE_ATTRIBUTES) {
        extra.push_back("-XX:StartFlightRecording=delay=90s,duration=60s,settings=profile,filename=" + local + "\\bench.jfr");
    }
    std::vector<JavaVMOption> vmOpts;
    for (auto& o : extra) {
        LogLine("jvm option: %s", o.c_str());
        vmOpts.push_back({ const_cast<char*>(o.c_str()), nullptr });
    }
    for (auto& o : opts) {
        LogLine("jvm option: %s", o.c_str());
        vmOpts.push_back({ const_cast<char*>(o.c_str()), nullptr });
    }
    vmOpts.push_back({ const_cast<char*>("vfprintf"), reinterpret_cast<void*>(&JvmVfprintf) });
    vmOpts.push_back({ const_cast<char*>("exit"), reinterpret_cast<void*>(&JvmExit) });
    vmOpts.push_back({ const_cast<char*>("abort"), reinterpret_cast<void*>(&JvmAbort) });

    JavaVMInitArgs args{};
    args.version = JNI_VERSION_10;
    args.nOptions = static_cast<jint>(vmOpts.size());
    args.options = vmOpts.data();
    args.ignoreUnrecognized = JNI_FALSE;

    JavaVM* vm = nullptr;
    JNIEnv* env = nullptr;
    jint rc = create(&vm, reinterpret_cast<void**>(&env), &args);
    if (rc != JNI_OK) {
        LogLine("JNI_CreateJavaVM failed: %d", rc);
        SetJava(JavaState::Failed);
        return;
    }
    LogLine("jvm started in %.0f ms", elapsed());

    jclass cls = env->FindClass(kMainClass);
    jmethodID main = cls ? env->GetStaticMethodID(cls, "main", "([Ljava/lang/String;)V") : nullptr;
    if (!main) {
        LogLine("%s.main not found: %s", kMainClass, ExceptionText(env).c_str());
        SetJava(JavaState::Failed);
        return;
    }
    SetJava(JavaState::Running);
    jobjectArray noArgs = env->NewObjectArray(0, env->FindClass("java/lang/String"), nullptr);
    env->CallStaticVoidMethod(cls, main, noArgs);
    if (env->ExceptionCheck()) {
        LogLine("%s.main threw: %s", kMainClass, ExceptionText(env).c_str());
        SetJava(JavaState::Failed);
        return;
    }
    LogLine("%s.main returned after %.0f ms", kMainClass, elapsed());
    SetJava(JavaState::Done);
}

DWORD WINAPI JavaThread(void*)
{
    RunJava();
    return 0;
}

void StartJava()
{
    // the jvm wants a big stack on the thread that creates it
    HANDLE t = CreateThread(nullptr, 16 * 1024 * 1024, JavaThread, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!t) {
        LogLine("CreateThread for java failed, error %lu", GetLastError());
        SetJava(JavaState::Failed);
        return;
    }
    CloseHandle(t);
}

// ---- fallback color, straight through angle (linked into the exe) ----

struct Fallback {
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLSurface surf = EGL_NO_SURFACE;
    bool ok = false;

    void Init(CoreWindow const& window)
    {
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
        ok = getPlatformDisplay &&
             (dpy = getPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, displayAttribs)) != EGL_NO_DISPLAY &&
             eglInitialize(dpy, nullptr, nullptr) && eglBindAPI(EGL_OPENGL_ES_API) &&
             eglChooseConfig(dpy, configAttribs, &config, 1, &count) && count > 0;
        if (ok) {
            surf = eglCreateWindowSurface(dpy, config, reinterpret_cast<EGLNativeWindowType>(get_abi(window)), nullptr);
            EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, contextAttribs);
            ok = surf != EGL_NO_SURFACE && ctx != EGL_NO_CONTEXT && eglMakeCurrent(dpy, surf, surf, ctx);
        }
        if (!ok) LogLine("fallback angle context failed: 0x%x", eglGetError());
    }

    void Paint(float r, float g, float b)
    {
        if (!ok) return;
        glClearColor(r, g, b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        eglSwapBuffers(dpy, surf);
    }
};

struct App : implements<App, IFrameworkViewSource, IFrameworkView> {
    CoreWindow m_window{ nullptr };
    bool m_closed = false;

    IFrameworkView CreateView() { return *this; }

    void Initialize(CoreApplicationView const&) { OpenLog(); }

    void SetWindow(CoreWindow const& window)
    {
        m_window = window;
        window.Closed([this](auto&&, auto&&) { m_closed = true; });
    }

    void Load(hstring const&) {}

    void Uninitialize() {}

    void Run()
    {
        m_window.Activate();
        g_dispatcher = m_window.Dispatcher();

        uint64_t limit = Windows::System::MemoryManager::AppMemoryUsageLimit();
        LogLine("AppMemoryUsageLimit: %llu bytes (%.2f GB)", limit,
                static_cast<double>(limit) / (1024.0 * 1024.0 * 1024.0));

        // mobileglues reads this with getenv when it starts
        std::string mgDir = Narrow(LocalDir() + L"\\MG");
        CreateDirectoryA(mgDir.c_str(), nullptr);
        _putenv_s("MG_DIR_PATH", mgDir.c_str());
        // SDL2 (controllable on 1.21.11) reads its settings from the environment.
        // xinput, rawinput, directinput and hid are desktop ways to reach a pad
        // that don't work in the xbox sandbox, Windows.Gaming.Input is the one that does
        _putenv_s("SDL_JOYSTICK_WGI", "1");
        _putenv_s("SDL_XINPUT_ENABLED", "0");
        _putenv_s("SDL_JOYSTICK_RAWINPUT", "0");
        _putenv_s("SDL_DIRECTINPUT_ENABLED", "0");
        _putenv_s("SDL_JOYSTICK_HIDAPI", "0");
        // no-error gl context, switched on the log server's settings page
        if (GetFileAttributesW((LocalDir() + L"\\noerror.on").c_str()) != INVALID_FILE_ATTRIBUTES) {
            _putenv_s("XMC_NO_ERROR", "1");
            LogLine("no-error gl context: on");
        }
        // mobileglues settings, only if there's no config yet. enableNoError 2:
        // when a shader program doesn't link it logs the real reason (its other
        // error logging is compiled out) and lets it through instead of crashing
        std::string mgConfig = mgDir + "\\config.json";
        if (GetFileAttributesA(mgConfig.c_str()) == INVALID_FILE_ATTRIBUTES) {
            FILE* f = nullptr;
            if (fopen_s(&f, mgConfig.c_str(), "w") == 0 && f) {
                fputs("{\"enableANGLE\":0,\"enableNoError\":2,\"enableExtComputeShader\":0,\"enableExtTimerQuery\":0,"
                      "\"enableExtDirectStateAccess\":0,\"maxGlslCacheSize\":0,\"angleDepthClearFixMode\":0,"
                      "\"customGLVersion\":40,\"fsr1Setting\":0,\"hideMGEnvLevel\":0}\n", f);
                fclose(f);
            }
        }

        DeleteFileW((LocalDir() + L"\\logurl.txt").c_str());
        StartLogServer();
        RunShaderTestIfAsked();

        bool glfwOk = AttachGlfw(m_window);
        if (glfwOk) StartJava();

        Fallback fallback;
        bool painting = false;
        float r = 0, g = 0, b = 0;
        JavaState shown = JavaState::Starting;
        auto dispatcher = m_window.Dispatcher();
        while (!m_closed) {
            JavaState js = g_java.load();
            if (!painting && (!glfwOk || js == JavaState::NoJvmDll || js == JavaState::Failed ||
                              js == JavaState::Done)) {
                if (!glfwOk) { r = 0.1f; g = 0.3f; b = 0.9f; LogLine("result: blue"); }
                else if (js == JavaState::NoJvmDll) { r = 0.8f; g = 0.0f; b = 0.0f; LogLine("result: red"); }
                else if (js == JavaState::Failed) { r = 1.0f; g = 0.5f; b = 0.0f; LogLine("result: orange"); }
                if (js != JavaState::Done && !(g_glfwHasSurface && g_glfwHasSurface())) {
                    fallback.Init(m_window);
                    painting = true;
                } else if (js != shown) {
                    LogLine(js == JavaState::Done ? "java finished" : "java failed after the window was up, see java.log");
                }
            }
            shown = js;

            if (painting) {
                dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
                fallback.Paint(r, g, b);
                if (!fallback.ok) dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
            } else {
                // java draws, the ui thread only pumps events (glfw3.dll queues them)
                dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
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
