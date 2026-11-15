// tiny web server for the logs, so I don't have to dig them out of LocalState
// with the Device Portal every time. http://<xbox ip>:8080 on my home network.
// only serves a fixed list of log files (never account.json or the client id).
// included from main.cpp inside its namespace, uses LocalDir/Narrow/LogLine.

// its #includes are at the top of main.cpp (this is included inside a namespace)

constexpr const wchar_t* kLogPort = L"8080";

winrt::Windows::Networking::Sockets::StreamSocketListener g_logListener{ nullptr };

// the logs that can be fetched, by the name used in the url
struct LogFile { std::string name; std::filesystem::path path; };

std::vector<LogFile> ListLogs()
{
    namespace fs = std::filesystem;
    fs::path local = LocalDir();
    std::vector<LogFile> out;
    const char* fixed[][2] = {
        { "launcher.log", "launcher.log" },
        { "latest.log", "game\\logs\\latest.log" },
        { "host.log", "host.log" },
        { "glfw.log", "glfw.log" },
        { "mobileglues.log", "MG\\latest.log" },
    };
    for (auto& f : fixed) {
        fs::path p = local / f[1];
        std::error_code ec;
        if (fs::exists(p, ec)) out.push_back({ f[0], p });
    }
    // crash reports (newest first) and jvm crash logs
    std::vector<std::pair<fs::file_time_type, LogFile>> extra;
    auto scan = [&](fs::path dir, std::regex const& re) {
        std::error_code ec;
        for (auto& e : fs::directory_iterator(dir, ec)) {
            std::string n = e.path().filename().string();
            if (e.is_regular_file(ec) && std::regex_match(n, re))
                extra.push_back({ e.last_write_time(ec), { n, e.path() } });
        }
    };
    scan(local / "game" / "crash-reports", std::regex(R"(crash-[\w.\-]+\.txt)"));
    scan(local, std::regex(R"(hs_err_pid\d+\.log)"));
    std::sort(extra.begin(), extra.end(), [](auto& a, auto& b) { return a.first > b.first; });
    for (auto& e : extra) out.push_back(e.second);
    return out;
}

// reads a file that may still be open for writing, only the last 4 MB
std::string ReadShared(std::filesystem::path const& p)
{
    HANDLE h = CreateFile2(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           OPEN_EXISTING, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "(can't open the file)";
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    const long long maxBytes = 4LL << 20;
    std::string text;
    if (size.QuadPart > maxBytes) {
        LARGE_INTEGER pos{};
        pos.QuadPart = size.QuadPart - maxBytes;
        SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);
        text = "(only the last 4 MB)\n";
    }
    std::vector<char> buf(1 << 16);
    DWORD n = 0;
    while (ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr) && n > 0) text.append(buf.data(), n);
    CloseHandle(h);
    return text;
}

std::string HtmlEscape(std::string const& s)
{
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '&') o += "&amp;";
        else o += c;
    }
    return o;
}

std::string IndexPage()
{
    std::string h = "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
                    "<title>xbox-mc logs</title><style>body{font:15px sans-serif;margin:16px;background:#16161a;color:#ddd}"
                    "a{color:#7fd4a0;display:block;padding:6px 0}</style><h2>xbox-mc logs</h2>";
    for (auto& f : ListLogs()) {
        std::error_code ec;
        auto kb = std::filesystem::file_size(f.path, ec) / 1024;
        h += "<a href='/log/" + f.name + "'>" + HtmlEscape(f.name) + " (" + std::to_string(kb) + " KB)</a>";
    }
    h += "<p style='color:#888'>raw text: add ?raw to a log link</p><p><a href='/settings'>settings</a></p>";
    return h;
}

std::string Response(int code, const char* type, std::string const& body)
{
    const char* status = code == 200 ? "200 OK" : code == 404 ? "404 Not Found" : "400 Bad Request";
    return std::string("HTTP/1.1 ") + status + "\r\nContent-Type: " + type +
           "\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\n\r\n" + body;
}

// settings that are easier to flip from the phone than with a new build.
// only the no-error switch for now, read by the host on the next start
std::string SettingsPage(std::string const& query)
{
    std::wstring flag = LocalDir() + L"\\noerror.on";
    if (query == "noerror=1") {
        FILE* f = nullptr;
        if (_wfopen_s(&f, flag.c_str(), L"w") == 0 && f) fclose(f);
    } else if (query == "noerror=0") {
        DeleteFileW(flag.c_str());
    }
    bool on = GetFileAttributesW(flag.c_str()) != INVALID_FILE_ATTRIBUTES;
    std::string h = "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
                    "<title>xbox-mc settings</title><style>body{font:15px sans-serif;margin:16px;background:#16161a;color:#ddd}"
                    "a{color:#7fd4a0}</style><h2>xbox-mc settings</h2>"
                    "<p>No-error GL context: <b>" + std::string(on ? "on" : "off") + "</b> &middot; "
                    "<a href='/settings?noerror=" + std::string(on ? "0" : "1") + "'>turn " + (on ? "off" : "on") + "</a></p>"
                    "<p style='color:#888'>skips GL error checking, less CPU per draw call. if the game glitches or "
                    "crashes with it on, turn it off. applies the next time the app starts.</p>"
                    "<p><a href='/'>logs</a></p>";
    return Response(200, "text/html; charset=utf-8", h);
}

std::string Handle(std::string const& path)
{
    if (path == "/" || path.empty()) return Response(200, "text/html; charset=utf-8", IndexPage());
    if (path.rfind("/settings", 0) == 0) {
        auto q = path.find('?');
        return SettingsPage(q == std::string::npos ? "" : path.substr(q + 1));
    }
    std::string name = path, query;
    if (auto q = name.find('?'); q != std::string::npos) {
        query = name.substr(q + 1);
        name = name.substr(0, q);
    }
    if (name.rfind("/log/", 0) == 0) {
        name = name.substr(5);
        for (auto& f : ListLogs()) {
            if (f.name != name) continue;
            std::string text = ReadShared(f.path);
            if (query == "raw") return Response(200, "text/plain; charset=utf-8", text);
            std::string page = "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
                               "<title>" + HtmlEscape(name) + "</title><style>body{margin:0;background:#16161a;color:#ddd}"
                               "pre{font:12px monospace;padding:12px;white-space:pre-wrap;word-break:break-all}"
                               "a{color:#7fd4a0}</style><p style='padding:0 12px'><a href='/'>back</a> &middot; <a href='?raw'>raw</a></p>"
                               "<pre>" + HtmlEscape(text) + "</pre><script>scrollTo(0,document.body.scrollHeight)</script>";
            return Response(200, "text/html; charset=utf-8", page);
        }
    }
    return Response(404, "text/plain", "not found\n");
}

void ServeOne(winrt::Windows::Networking::Sockets::StreamSocket sock)
{
    using namespace winrt::Windows::Storage::Streams;
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    try {
        DataReader reader(sock.InputStream());
        reader.InputStreamOptions(InputStreamOptions::Partial);
        std::string req;
        while (req.find("\r\n\r\n") == std::string::npos && req.size() < 8192) {
            uint32_t n = reader.LoadAsync(4096).get();
            if (n == 0) break;
            std::vector<uint8_t> bytes(n);
            reader.ReadBytes(bytes);
            req.append(bytes.begin(), bytes.end());
        }
        std::string reply;
        if (req.rfind("GET ", 0) == 0) {
            auto end = req.find(' ', 4);
            reply = Handle(req.substr(4, end == std::string::npos ? std::string::npos : end - 4));
        } else {
            reply = Response(400, "text/plain", "only GET\n");
        }
        DataWriter writer(sock.OutputStream());
        writer.WriteBytes(winrt::array_view<const uint8_t>(reinterpret_cast<const uint8_t*>(reply.data()),
                                                           static_cast<uint32_t>(reply.size())));
        writer.StoreAsync().get();
        writer.FlushAsync().get();
        writer.DetachStream();
    } catch (...) {
    }
    sock.Close();
}

// the xbox's address on the home network, for the launcher screen
std::string LocalIpv4()
{
    using namespace winrt::Windows::Networking;
    std::string any;
    try {
        // prefer the adapter that has the internet connection (a pc can have
        // virtual ones too, wsl/hyper-v)
        winrt::guid inet{};
        bool haveInet = false;
        if (auto profile = Connectivity::NetworkInformation::GetInternetConnectionProfile()) {
            if (auto adapter = profile.NetworkAdapter()) {
                inet = adapter.NetworkAdapterId();
                haveInet = true;
            }
        }
        for (auto const& host : Connectivity::NetworkInformation::GetHostNames()) {
            if (host.Type() != HostNameType::Ipv4 || !host.IPInformation()) continue;
            std::string ip = Narrow(std::wstring{ host.CanonicalName() });
            if (haveInet && host.IPInformation().NetworkAdapter().NetworkAdapterId() == inet) return ip;
            if (any.empty()) any = ip;
        }
    } catch (...) {
    }
    return any;
}

void StartLogServer()
{
    std::thread([] {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        try {
            g_logListener = winrt::Windows::Networking::Sockets::StreamSocketListener();
            g_logListener.ConnectionReceived([](auto const&, auto const& args) {
                auto sock = args.Socket();
                std::thread([sock] { ServeOne(sock); }).detach();
            });
            g_logListener.BindServiceNameAsync(kLogPort).get();
            std::string ip = LocalIpv4();
            std::string url = "http://" + (ip.empty() ? std::string("<xbox ip>") : ip) + ":" + Narrow(kLogPort);
            LogLine("log server: %s", url.c_str());
            // the launcher shows this on screen
            FILE* f = nullptr;
            std::wstring p = LocalDir() + L"\\logurl.txt";
            if (_wfopen_s(&f, p.c_str(), L"w") == 0 && f) {
                fputs(url.c_str(), f);
                fclose(f);
            }
        } catch (winrt::hresult_error const& e) {
            LogLine("log server failed: 0x%08x %ls", static_cast<unsigned>(e.code()), e.message().c_str());
        }
    }).detach();
}
