// shader link test straight on ANGLE (no minecraft, no mobileglues).
// runs on every start for now (a few ms). links every shadertest\*.vert
// in the package with shadertest\sodium.frag, once at the normal D3D feature
// level and once capped at 10.1 like the xbox one, and logs what ANGLE says.
// included from main.cpp inside its namespace (uses LogLine, LocalDir, InstallDir)

std::string ReadText(std::wstring const& path)
{
    std::string s;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

void ShaderTestAt(int maxMajor, int maxMinor)
{
    auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    std::vector<EGLint> attribs = { EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE };
    if (maxMajor > 0) {
        attribs.insert(attribs.end(), { EGL_PLATFORM_ANGLE_MAX_VERSION_MAJOR_ANGLE, maxMajor,
                                        EGL_PLATFORM_ANGLE_MAX_VERSION_MINOR_ANGLE, maxMinor });
    }
    attribs.push_back(EGL_NONE);
    EGLDisplay dpy = getPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, attribs.data());
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, nullptr, nullptr)) {
        LogLine("shadertest: no display for feature level %d.%d (0x%x)", maxMajor, maxMinor, eglGetError());
        return;
    }
    const EGLint cfgAttribs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                  EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE };
    EGLConfig cfg = nullptr;
    EGLint count = 0;
    eglChooseConfig(dpy, cfgAttribs, &cfg, 1, &count);
    const EGLint pb[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pb);
    const EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttribs);
    if (count < 1 || surf == EGL_NO_SURFACE || ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, surf, surf, ctx)) {
        LogLine("shadertest: no ES3 context at %d.%d (0x%x)", maxMajor, maxMinor, eglGetError());
        eglTerminate(dpy);
        return;
    }
    LogLine("shadertest: === max feature level %s: %s", maxMajor ? (std::to_string(maxMajor) + "." + std::to_string(maxMinor)).c_str() : "default",
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    GLint maxVsVectors = 0, maxBlockSize = 0, maxVsBlocks = 0;
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &maxVsVectors);
    glGetIntegerv(0x8A30 /*GL_MAX_UNIFORM_BLOCK_SIZE*/, &maxBlockSize);
    glGetIntegerv(0x8A2B /*GL_MAX_VERTEX_UNIFORM_BLOCKS*/, &maxVsBlocks);
    LogLine("shadertest: max vertex uniform vectors %d, max block size %d, max vertex blocks %d", maxVsVectors, maxBlockSize, maxVsBlocks);

    std::wstring dir = InstallDir() + L"\\shadertest\\";
    std::string fragSrc = ReadText(dir + L"sodium.frag");
    auto compile = [](GLenum type, std::string const& src, std::string& log) {
        GLuint s = glCreateShader(type);
        const char* p = src.c_str();
        glShaderSource(s, 1, &p, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        char buf[2048] = {};
        glGetShaderInfoLog(s, sizeof(buf), nullptr, buf);
        log = buf;
        return ok ? s : 0u;
    };
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (e.path().extension() != L".vert") continue;
        std::string name = e.path().filename().string();
        std::string vlog, flog;
        GLuint vs = compile(GL_VERTEX_SHADER, ReadText(e.path().wstring()), vlog);
        GLuint fs = compile(GL_FRAGMENT_SHADER, fragSrc, flog);
        if (!vs || !fs) {
            LogLine("shadertest: %s compile failed: vs=%u fs=%u\n%s\n%s", name.c_str(), vs, fs, vlog.c_str(), flog.c_str());
            continue;
        }
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);
        GLint ok = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        char buf[4096] = {};
        glGetProgramInfoLog(prog, sizeof(buf), nullptr, buf);
        LogLine("shadertest: %-22s link %s %s", name.c_str(), ok ? "OK" : "FAILED", buf);
        glDeleteProgram(prog);
        glDeleteShader(vs);
        glDeleteShader(fs);
    }
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglDestroySurface(dpy, surf);
    eglTerminate(dpy);
}

void RunShaderTestIfAsked()
{
    // for now it always runs, it only takes a few ms (sodium on the xbox hunt)
    ShaderTestAt(0, 0);
    ShaderTestAt(10, 1);
}
