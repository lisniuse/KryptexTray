#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace fs = std::filesystem;

static const wchar_t* kClassName = L"KryptexTrayWnd";
static const wchar_t* kOverlayClass = L"KryptexOverlayWnd";
static const wchar_t* kHardwareUrl = L"https://www.kryptex.com/site/hardware";
static const UINT WM_TRAY = WM_APP + 1;
static const UINT WM_REFRESH_DONE = WM_APP + 2;
static const UINT_PTR TIMER_REFRESH = 1;

static const UINT kMenuOverlay = 1001;
static const UINT kMenuRefresh = 1002;
static const UINT kMenuExit = 1003;

struct Config {
    std::wstring cookie;
    std::wstring url = L"https://www.kryptex.com/site/dashboard";
    int refreshSeconds = 60;
    fs::path envPath;
};

struct WorkerInfo {
    std::wstring name;
    std::wstring coin;
    std::wstring scheme;
    std::wstring agent;
    bool online = false;
    double hash30m = 0;
    double hash24h = 0;
    double profitDaily = 0;
    double profitMonthly = 0;
    int valid = 0;
    int stale = 0;
    int invalid = 0;
};

struct RefreshResult {
    bool balanceOk = false;
    bool hardwareOk = false;
    std::wstring btc;
    std::wstring fiat;
    std::wstring balanceError;
    std::wstring hardwareError;
    std::vector<WorkerInfo> workers;
    double daily = 0;
    double monthly = 0;
    double prlHash = 0;
    double qtcHash = 0;
    int online = 0;
};

static HINSTANCE g_hinst = nullptr;
static HWND g_hwnd = nullptr;
static HWND g_overlay = nullptr;
static bool g_overlayVisible = false;
static bool g_overlayClassRegistered = false;
static std::wstring g_fontFace;
static std::vector<BYTE> g_bg;
static int g_bgW = 0, g_bgH = 0, g_bgX = 0, g_bgY = 0;
static bool g_forceBackdrop = false;
static NOTIFYICONDATAW g_nid{};
static UINT g_taskbarCreated = 0;

static Config g_cfg;
static bool g_busy = false;
static bool g_haveBalance = false;
static bool g_haveHardware = false;
static std::wstring g_btc = L"—";
static std::wstring g_fiat;
static std::wstring g_status = L"正在启动…";
static std::wstring g_hardwareError;
static std::vector<WorkerInfo> g_workers;
static double g_daily = 0;
static double g_monthly = 0;
static double g_prlHash = 0;
static double g_qtcHash = 0;
static int g_online = 0;

static void RefreshOverlay();

static std::wstring TrimW(std::wstring s) {
    auto ws = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; };
    size_t a = 0, b = s.size();
    while (a < b && ws(s[a])) ++a;
    while (b > a && ws(s[b - 1])) --b;
    return s.substr(a, b - a);
}

static std::string TrimA(std::string s) {
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    size_t a = 0, b = s.size();
    while (a < b && ws(s[a])) ++a;
    while (b > a && ws(s[b - 1])) --b;
    return s.substr(a, b - a);
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

static std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
}

static std::string UnquoteA(std::string v) {
    v = TrimA(std::move(v));
    if (v.size() >= 2) {
        char a = v.front(), b = v.back();
        if ((a == '"' && b == '"') || (a == '\'' && b == '\''))
            v = v.substr(1, v.size() - 2);
    }
    return v;
}

static fs::path ExePath() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(std::wstring(buf, n));
}

static fs::path FindEnvFile() {
    fs::path exe = ExePath();
    fs::path dir = exe.parent_path();
    std::error_code cwdEc;
    fs::path cwd = fs::current_path(cwdEc);
    fs::path candidates[] = {
        dir / L".env",
        dir.parent_path() / L".env",
        cwdEc ? fs::path() : cwd / L".env",
    };
    for (const auto& p : candidates) {
        std::error_code ec;
        if (fs::exists(p, ec) && !ec) return p;
    }
    return dir.parent_path() / L".env";
}

static bool LoadConfig(Config& cfg, std::wstring& err) {
    cfg = Config{};
    cfg.envPath = FindEnvFile();

    std::ifstream in(cfg.envPath, std::ios::binary);
    if (!in) {
        err = L"找不到 .env 文件：\n" + cfg.envPath.wstring() +
              L"\n\n请把 .env.example 复制成 .env 并填入 KRYPTEX_COOKIE。";
        return false;
    }

    std::stringstream buf;
    buf << in.rdbuf();
    std::string content = buf.str();
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        content.erase(0, 3);
    }

    std::istringstream lines(content);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = TrimA(line);
        if (t.empty() || t[0] == '#') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = TrimA(t.substr(0, eq));
        std::string val = UnquoteA(t.substr(eq + 1));
        if (key.rfind("export ", 0) == 0) key = TrimA(key.substr(7));

        if (key == "KRYPTEX_COOKIE") {
            cfg.cookie = Utf8ToWide(val);
        } else if (key == "KRYPTEX_URL") {
            if (!val.empty()) cfg.url = Utf8ToWide(val);
        } else if (key == "REFRESH_SECONDS") {
            try {
                int sec = std::stoi(val);
                cfg.refreshSeconds = std::clamp(sec, 10, 3600);
            } catch (...) {
            }
        }
    }

    if (cfg.cookie.empty()) {
        err = L".env 里没有配置 KRYPTEX_COOKIE。\n\n路径：" + cfg.envPath.wstring();
        return false;
    }
    return true;
}

static bool ParseHttpsUrl(const std::wstring& url, std::wstring& host, std::wstring& path, std::wstring& err) {
    const std::wstring prefix = L"https://";
    if (url.size() <= prefix.size() || url.compare(0, prefix.size(), prefix) != 0) {
        err = L"KRYPTEX_URL 必须以 https:// 开头";
        return false;
    }
    std::wstring rest = url.substr(prefix.size());
    size_t slash = rest.find(L'/');
    if (slash == std::wstring::npos) {
        host = rest;
        path = L"/";
    } else {
        host = rest.substr(0, slash);
        path = rest.substr(slash);
    }
    if (host.empty()) {
        err = L"KRYPTEX_URL 缺少主机名";
        return false;
    }
    return true;
}

static bool HttpGet(const std::wstring& url, const std::wstring& cookie,
                    std::string& body, DWORD& status, std::wstring& err) {
    std::wstring host, path;
    if (!ParseHttpsUrl(url, host, path, err)) return false;

    HINTERNET session = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        L"(KHTML, like Gecko) Chrome/155.0.0.0 Safari/537.36",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        err = L"WinHttpOpen 失败";
        return false;
    }
    WinHttpSetTimeouts(session, 10000, 10000, 10000, 30000);

    HINTERNET connect = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        err = L"连接 " + host + L" 失败";
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(
        connect, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) {
        err = L"创建 HTTP 请求失败";
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));

    std::wstring headers =
        L"Cookie: " + cookie + L"\r\n"
        L"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
        L"Accept-Language: zh-CN,zh;q=0.9,en;q=0.8\r\n"
        L"Referer: https://www.kryptex.com/site/dashboard\r\n";

    bool ok = false;
    do {
        if (!WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(-1),
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            err = L"发送请求失败，错误码 " + std::to_wstring(GetLastError());
            break;
        }
        if (!WinHttpReceiveResponse(request, nullptr)) {
            err = L"接收响应失败，错误码 " + std::to_wstring(GetLastError());
            break;
        }

        DWORD code = 0, len = sizeof(code);
        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX)) {
            err = L"读取 HTTP 状态码失败";
            break;
        }
        status = code;

        body.clear();
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(request, &avail)) {
                err = L"读取响应长度失败";
                break;
            }
            if (avail == 0) break;
            std::string chunk(avail, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), avail, &read)) {
                err = L"读取响应内容失败";
                break;
            }
            body.append(chunk.data(), read);
            if (body.size() > 4 * 1024 * 1024) break;
        }
        ok = true;
    } while (false);

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return ok;
}

static bool ParseBalance(const std::string& utf8, std::wstring& btc, std::wstring& fiat, std::wstring& err) {
    std::wstring html = Utf8ToWide(utf8);
    if (html.empty()) {
        err = L"响应内容为空";
        return false;
    }

    std::wregex reBtc(L"(\\d+\\.\\d+)\\s*BTC");
    std::wregex reFiat(L"≈\\s*([^<\\r\\n]{1,24})");
    std::wsmatch m;

    if (!std::regex_search(html, m, reBtc)) {
        err = L"页面中没找到 BTC 余额，Cookie 可能已失效";
        return false;
    }
    btc = m[1].str();

    fiat.clear();
    if (std::regex_search(html, m, reFiat)) {
        fiat = TrimW(m[1].str());
    }
    return true;
}

static double ToDouble(const std::string& s) {
    try {
        return std::stod(s);
    } catch (...) {
        return 0;
    }
}

static int ToInt(const std::string& s) {
    try {
        return std::stoi(s);
    } catch (...) {
        return 0;
    }
}

static void AppendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

static bool ParseHex4(const std::string& s, size_t pos, unsigned& out) {
    if (pos + 4 > s.size()) return false;
    unsigned v = 0;
    for (size_t i = 0; i < 4; ++i) {
        char c = s[pos + i];
        unsigned d;
        if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
        else return false;
        v = (v << 4) | d;
    }
    out = v;
    return true;
}

class JsonReader {
public:
    explicit JsonReader(const std::string& text) : s_(text) {}

    bool ParseArrayOfObjects(std::vector<std::map<std::string, std::string>>& out) {
        if (!Consume('[')) return false;
        if (Consume(']')) return true;
        do {
            std::map<std::string, std::string> obj;
            if (!ParseObject(obj)) return false;
            out.push_back(std::move(obj));
        } while (Consume(','));
        return Consume(']');
    }

private:
    const std::string& s_;
    size_t i_ = 0;

    void SkipWs() {
        while (i_ < s_.size()) {
            char c = s_[i_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++i_;
            else break;
        }
    }

    bool Consume(char c) {
        SkipWs();
        if (i_ < s_.size() && s_[i_] == c) {
            ++i_;
            return true;
        }
        return false;
    }

    bool ParseString(std::string& out) {
        SkipWs();
        if (i_ >= s_.size() || s_[i_] != '"') return false;
        ++i_;
        out.clear();
        while (i_ < s_.size()) {
            char c = s_[i_++];
            if (c == '"') return true;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (i_ >= s_.size()) return false;
            char e = s_[i_++];
            switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                unsigned cp = 0;
                if (!ParseHex4(s_, i_, cp)) return false;
                i_ += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && i_ + 6 <= s_.size() &&
                    s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                    unsigned low = 0;
                    if (ParseHex4(s_, i_ + 2, low) && low >= 0xDC00 && low <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        i_ += 6;
                    }
                }
                AppendUtf8(out, cp);
                break;
            }
            default:
                out.push_back(e);
                break;
            }
        }
        return false;
    }

    bool ParseScalar(std::string& out) {
        SkipWs();
        if (i_ >= s_.size()) return false;
        if (s_[i_] == '"' || s_[i_] == '{' || s_[i_] == '[') return false;
        size_t start = i_;
        while (i_ < s_.size()) {
            char c = s_[i_];
            if (c == ',' || c == '}' || c == ']' || c == ' ' || c == '\t' ||
                c == '\r' || c == '\n') {
                break;
            }
            ++i_;
        }
        out = s_.substr(start, i_ - start);
        return !out.empty();
    }

    bool SkipValue() {
        SkipWs();
        if (i_ >= s_.size()) return false;
        if (s_[i_] == '"') {
            std::string tmp;
            return ParseString(tmp);
        }
        if (s_[i_] == '{') {
            ++i_;
            if (Consume('}')) return true;
            do {
                std::string key;
                if (!ParseString(key) || !Consume(':')) return false;
                if (!SkipValue()) return false;
            } while (Consume(','));
            return Consume('}');
        }
        if (s_[i_] == '[') {
            ++i_;
            if (Consume(']')) return true;
            do {
                if (!SkipValue()) return false;
            } while (Consume(','));
            return Consume(']');
        }
        std::string tmp;
        return ParseScalar(tmp);
    }

    bool ParseObject(std::map<std::string, std::string>& obj) {
        if (!Consume('{')) return false;
        if (Consume('}')) return true;
        do {
            std::string key;
            if (!ParseString(key) || !Consume(':')) return false;
            SkipWs();
            std::string val;
            if (i_ < s_.size() && (s_[i_] == '{' || s_[i_] == '[')) {
                if (!SkipValue()) return false;
            } else if (i_ < s_.size() && s_[i_] == '"') {
                if (!ParseString(val)) return false;
            } else {
                if (!ParseScalar(val)) return false;
            }
            obj[key] = std::move(val);
        } while (Consume(','));
        return Consume('}');
    }
};

static std::string ExtractWorkerDataset(const std::string& html) {
    size_t p = html.find("workerDataset");
    if (p == std::string::npos) return {};
    size_t open = html.find('>', p);
    if (open == std::string::npos) return {};
    size_t end = html.find("</script>", open + 1);
    if (end == std::string::npos) return {};
    return html.substr(open + 1, end - open - 1);
}

static std::wstring UpperW(std::wstring s) {
    for (auto& c : s) {
        if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - 32);
    }
    return s;
}

static std::wstring LowerW(const std::wstring& s) {
    std::wstring out = s;
    for (auto& c : out) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + 32);
    }
    return out;
}

static std::wstring CoinName(const std::string& coin) {
    std::wstring out = UpperW(Utf8ToWide(coin));
    return out.empty() ? L"?" : out;
}

static std::wstring SchemeName(const std::string& scheme) {
    std::wstring out = UpperW(Utf8ToWide(scheme));
    return out.empty() ? L"?" : out;
}

static bool ParseHardware(const std::string& html, RefreshResult& r, std::wstring& err) {
    std::string json = ExtractWorkerDataset(html);
    if (json.empty()) {
        err = L"硬件页中没找到 workerDataset，Cookie 可能已失效";
        return false;
    }

    std::vector<std::map<std::string, std::string>> items;
    JsonReader reader(json);
    if (!reader.ParseArrayOfObjects(items)) {
        err = L"workerDataset 解析失败";
        return false;
    }

    r.workers.clear();
    r.daily = r.monthly = r.prlHash = r.qtcHash = 0;
    r.online = 0;

    for (const auto& item : items) {
        auto get = [&item](const char* key) -> const std::string& {
            static const std::string empty;
            auto it = item.find(key);
            return it == item.end() ? empty : it->second;
        };

        WorkerInfo w;
        w.name = Utf8ToWide(get("name"));
        if (w.name.empty()) w.name = L"(未命名)";
        w.coin = CoinName(get("coin"));
        w.scheme = SchemeName(get("scheme"));
        w.agent = Utf8ToWide(get("agent"));
        w.online = get("is_online") == "true";
        w.hash30m = ToDouble(get("avg_hashrate_30m"));
        w.hash24h = ToDouble(get("avg_hashrate_24h"));
        w.profitDaily = ToDouble(get("profit_daily"));
        w.profitMonthly = ToDouble(get("profit_monthly"));
        w.valid = ToInt(get("valid"));
        w.stale = ToInt(get("stale"));
        w.invalid = ToInt(get("invalid"));

        r.daily += w.profitDaily;
        r.monthly += w.profitMonthly;
        if (w.online) ++r.online;
        if (get("coin") == "prl") r.prlHash += w.hash30m;
        else if (get("coin") == "qtc") r.qtcHash += w.hash30m;

        r.workers.push_back(std::move(w));
    }

    std::sort(r.workers.begin(), r.workers.end(), [](const WorkerInfo& a, const WorkerInfo& b) {
        if (a.online != b.online) return a.online > b.online;
        return LowerW(a.name) < LowerW(b.name);
    });
    return true;
}

static std::wstring FormatHash(double h) {
    wchar_t buf[64];
    if (h >= 1e12) swprintf_s(buf, L"%.2f TH/s", h / 1e12);
    else if (h >= 1e9) swprintf_s(buf, L"%.2f GH/s", h / 1e9);
    else if (h >= 1e6) swprintf_s(buf, L"%.2f MH/s", h / 1e6);
    else if (h >= 1e3) swprintf_s(buf, L"%.2f kH/s", h / 1e3);
    else swprintf_s(buf, L"%.0f H/s", h);
    return buf;
}

static std::wstring FormatMoney(double v) {
    wchar_t buf[64];
    swprintf_s(buf, L"%.2f", v);
    return L"￥" + std::wstring(buf);
}

static int OverlayHeight() {
    int n = std::min(g_online, 8);
    if (n <= 0) return 120;
    int h = 92 + n * 28;
    if (g_online > 8) h += 14;
    return std::min(h, 500);
}

static void GetOverlayRect(int& x, int& y, int& w, int& h) {
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    w = 360;
    h = OverlayHeight();
    x = wa.right - w - 24;
    y = wa.top + 24;
}

static void BlurBackdrop(BYTE* px, int w, int h) {
    std::vector<BYTE> tmp(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    const int radius = 6;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int sb = 0, sg = 0, sr = 0, n = 0;
            int x0 = std::max(0, x - radius);
            int x1 = std::min(w - 1, x + radius);
            for (int xx = x0; xx <= x1; ++xx) {
                const BYTE* q = px + (static_cast<size_t>(y) * w + xx) * 4;
                sb += q[0]; sg += q[1]; sr += q[2]; ++n;
            }
            BYTE* o = tmp.data() + (static_cast<size_t>(y) * w + x) * 4;
            o[0] = static_cast<BYTE>(sb / n);
            o[1] = static_cast<BYTE>(sg / n);
            o[2] = static_cast<BYTE>(sr / n);
            o[3] = 255;
        }
    }

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int sb = 0, sg = 0, sr = 0, n = 0;
            int y0 = std::max(0, y - radius);
            int y1 = std::min(h - 1, y + radius);
            for (int yy = y0; yy <= y1; ++yy) {
                const BYTE* q = tmp.data() + (static_cast<size_t>(yy) * w + x) * 4;
                sb += q[0]; sg += q[1]; sr += q[2]; ++n;
            }
            BYTE* o = px + (static_cast<size_t>(y) * w + x) * 4;
            o[0] = static_cast<BYTE>(sb / n * 0.32);
            o[1] = static_cast<BYTE>(sg / n * 0.32);
            o[2] = static_cast<BYTE>(sr / n * 0.32);
            o[3] = 255;
        }
    }
}

static void CaptureBackdrop(int x, int y, int w, int h) {
    HDC screen = GetDC(nullptr);
    if (!screen) return;

    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib && bits) {
        HGDIOBJ old = SelectObject(mem, dib);
        BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY);
        SelectObject(mem, old);
        g_bg.assign(static_cast<BYTE*>(bits),
                    static_cast<BYTE*>(bits) + static_cast<size_t>(w) * h * 4);
        g_bgW = w; g_bgH = h; g_bgX = x; g_bgY = y;
        BlurBackdrop(g_bg.data(), w, h);
    }

    if (dib) DeleteObject(dib);
    if (mem) DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

static void UpdateTray() {
    std::wstring tip;
    if (g_haveBalance) {
        tip = L"Kryptex " + g_btc + L" BTC";
        if (!g_fiat.empty()) tip += L" ≈ " + g_fiat;
        if (g_haveHardware) tip += L" · " + FormatMoney(g_daily) + L"/天";
    } else {
        tip = L"Kryptex: " + g_status;
    }
    if (tip.size() > 127) tip.resize(127);
    wcscpy_s(g_nid.szTip, tip.c_str());
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    if (g_overlay && g_overlayVisible) RefreshOverlay();
}

static void ApplyTimer() {
    UINT ms = static_cast<UINT>(std::clamp(g_cfg.refreshSeconds, 10, 3600)) * 1000u;
    SetTimer(g_hwnd, TIMER_REFRESH, ms, nullptr);
}

static void RefreshWorker() {
    RefreshResult* r = new RefreshResult();

    Config cfg;
    std::wstring err;
    if (!LoadConfig(cfg, err)) {
        r->balanceError = err;
        r->hardwareError = err;
        PostMessageW(g_hwnd, WM_REFRESH_DONE, 0, reinterpret_cast<LPARAM>(r));
        return;
    }

    std::string body;
    DWORD status = 0;
    std::wstring reqErr;

    if (!HttpGet(cfg.url, cfg.cookie, body, status, reqErr)) {
        r->balanceError = reqErr;
    } else if (status != 200) {
        r->balanceError = L"HTTP " + std::to_wstring(status);
    } else if (!ParseBalance(body, r->btc, r->fiat, reqErr)) {
        r->balanceError = reqErr;
    } else {
        r->balanceOk = true;
    }

    body.clear();
    status = 0;
    reqErr.clear();

    if (!HttpGet(kHardwareUrl, cfg.cookie, body, status, reqErr)) {
        r->hardwareError = reqErr;
    } else if (status != 200) {
        r->hardwareError = L"HTTP " + std::to_wstring(status);
    } else if (!ParseHardware(body, *r, reqErr)) {
        r->hardwareError = reqErr;
    } else {
        r->hardwareOk = true;
    }

    PostMessageW(g_hwnd, WM_REFRESH_DONE, 0, reinterpret_cast<LPARAM>(r));
}

static void RefreshAsync() {
    if (g_busy) return;
    g_busy = true;
    g_status = L"正在更新…";
    UpdateTray();
    std::thread(RefreshWorker).detach();
}

static void PositionOverlay(HWND hwnd, int h) {
    int x, y, w, calcH;
    GetOverlayRect(x, y, w, calcH);
    SetWindowPos(hwnd, HWND_NOTOPMOST, x, y, w, h, SWP_NOACTIVATE);
}

static void RefreshOverlay() {
    if (!g_overlay || !g_overlayVisible) return;

    int x, y, w, h;
    GetOverlayRect(x, y, w, h);
    bool force = g_forceBackdrop;
    g_forceBackdrop = false;
    if (force || x != g_bgX || y != g_bgY || w != g_bgW || h != g_bgH) {
        ShowWindow(g_overlay, SW_HIDE);
        PositionOverlay(g_overlay, h);
        Sleep(20);
        CaptureBackdrop(x, y, w, h);
        ShowWindow(g_overlay, SW_SHOWNOACTIVATE);
    } else {
        PositionOverlay(g_overlay, h);
    }
    InvalidateRect(g_overlay, nullptr, TRUE);
}

static int CALLBACK FontEnumProc(const LOGFONTW* lf, const TEXTMETRICW*, DWORD, LPARAM lParam) {
    auto* faces = reinterpret_cast<std::vector<std::wstring>*>(lParam);
    if (lf->lfFaceName[0] != L'@') faces->push_back(lf->lfFaceName);
    return 1;
}

static void InitFontFace() {
    if (!g_fontFace.empty()) return;

    std::vector<std::wstring> faces;
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    HDC hdc = GetDC(nullptr);
    if (hdc) {
        EnumFontFamiliesExW(hdc, &lf, reinterpret_cast<FONTENUMPROCW>(FontEnumProc),
                            reinterpret_cast<LPARAM>(&faces), 0);
        ReleaseDC(nullptr, hdc);
    }

    const wchar_t* candidates[] = {
        L"思源黑体",
        L"Source Han Sans SC",
        L"Source Han Sans CN",
        L"Source Han Sans",
        L"Noto Sans SC",
        L"Noto Sans CJK SC",
        L"Microsoft YaHei UI",
        L"Microsoft YaHei",
        L"Segoe UI",
    };
    for (const wchar_t* candidate : candidates) {
        for (const auto& face : faces) {
            if (_wcsicmp(face.c_str(), candidate) == 0) {
                g_fontFace = face;
                break;
            }
        }
        if (!g_fontFace.empty()) break;
    }
    if (g_fontFace.empty()) g_fontFace = L"Microsoft YaHei UI";

    std::ofstream log(ExePath().parent_path() / L"font.log",
                      std::ios::binary | std::ios::trunc);
    if (log) {
        log << "selected: " << WideToUtf8(g_fontFace) << "\n";
        for (const auto& face : faces) {
            if (face.find(L"Source") != std::wstring::npos ||
                face.find(L"Noto") != std::wstring::npos ||
                face.find(L"YaHei") != std::wstring::npos ||
                face.find(L"思源") != std::wstring::npos) {
                log << "  " << WideToUtf8(face) << "\n";
            }
        }
    }
}

static HFONT MakeFont(int px, int weight) {
    InitFontFace();
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, g_fontFace.c_str());
}

static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC paintDc = BeginPaint(hwnd, &ps);
        (void)paintDc;

        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        const BYTE backgroundAlpha = 235;
        if (w <= 0 || h <= 0) {
            EndPaint(hwnd, &ps);
            return 0;
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HDC screenDc = GetDC(nullptr);
        HDC hdc = CreateCompatibleDC(screenDc);
        HBITMAP dib = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!hdc || !dib) {
            if (dib) DeleteObject(dib);
            if (hdc) DeleteDC(hdc);
            if (screenDc) ReleaseDC(nullptr, screenDc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        HGDIOBJ oldBmp = SelectObject(hdc, dib);

        if (bits) {
            BYTE* px = static_cast<BYTE*>(bits);
            if (g_bgW == w && g_bgH == h &&
                g_bg.size() == static_cast<size_t>(w) * static_cast<size_t>(h) * 4) {
                std::copy(g_bg.begin(), g_bg.end(), px);
            } else {
                for (int i = 0; i < w * h; ++i) {
                    px[i * 4 + 0] = 0;
                    px[i * 4 + 1] = 0;
                    px[i * 4 + 2] = 0;
                    px[i * 4 + 3] = backgroundAlpha;
                }
            }
        }

        SetBkMode(hdc, TRANSPARENT);
        SetBkColor(hdc, RGB(0, 0, 0));

        HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(70, 70, 70));
        HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, 0, 0, w, h, 20, 20);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        const COLORREF cText = RGB(235, 242, 250);
        const COLORREF cMuted = RGB(140, 168, 205);
        const COLORREF cGreen = RGB(92, 220, 150);
        const COLORREF cRed = RGB(240, 100, 100);
        const COLORREF cDivider = RGB(55, 55, 55);

        HFONT fBold = MakeFont(9, FW_SEMIBOLD);
        HFONT fNormal = MakeFont(9, FW_NORMAL);

        auto text = [&](const std::wstring& s, int x, int y, int w, int h, HFONT font,
                        COLORREF color, UINT format) {
            HGDIOBJ old = SelectObject(hdc, font);
            SetTextColor(hdc, color);
            RECT r{x, y, x + w, y + h};
            DrawTextW(hdc, s.c_str(), -1, &r, format | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hdc, old);
        };
        auto divider = [&](int y) {
            RECT r{12, y, 348, y + 1};
            HBRUSH b = CreateSolidBrush(cDivider);
            FillRect(hdc, &r, b);
            DeleteObject(b);
        };

        std::wstring status;
        COLORREF statusColor = cMuted;
        if (!g_hardwareError.empty()) {
            status = L"硬件数据获取失败";
            statusColor = cRed;
        } else if (!g_haveHardware) {
            status = L"硬件数据获取中…";
        } else {
            status = std::to_wstring(g_online) + L" 在线";
        }
        text(L"Kryptex", 12, 6, 120, 14, fBold, cText, DT_LEFT | DT_VCENTER);
        text(status, 180, 6, 168, 14, fNormal, statusColor, DT_RIGHT | DT_VCENTER);

        if (g_haveBalance) {
            std::wstring balance = g_btc + L" BTC";
            if (!g_fiat.empty()) balance += L"  ≈ " + g_fiat;
            text(balance, 12, 22, 336, 16, fBold, cText,
                 DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
        } else {
            text(g_status, 12, 22, 336, 16, fNormal, cRed,
                 DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
        }

        divider(40);

        text(L"盈利能力", 12, 46, 48, 14, fNormal, cMuted, DT_LEFT | DT_VCENTER);
        if (g_haveHardware) {
            std::wstring profit = FormatMoney(g_daily) + L"/天 · " +
                                  FormatMoney(g_monthly) + L"/月";
            text(profit, 62, 46, 286, 14, fNormal, cText,
                 DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
        } else {
            text(L"—", 62, 46, 286, 14, fNormal, cMuted,
                 DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
        }

        text(L"算力", 12, 60, 28, 14, fNormal, cMuted, DT_LEFT | DT_VCENTER);
        std::wstring hash;
        if (g_prlHash > 0) hash += L"PRL " + FormatHash(g_prlHash);
        if (g_qtcHash > 0) {
            if (!hash.empty()) hash += L"   ";
            hash += L"QTC " + FormatHash(g_qtcHash);
        }
        if (hash.empty()) hash = L"—";
        text(hash, 42, 60, 306, 14, fNormal, cText,
             DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);

        divider(78);

        int wy = 84;
        int shown = 0;
        for (const WorkerInfo& wk : g_workers) {
            if (!wk.online) continue;
            if (shown >= 8) break;

            HBRUSH dotBrush = CreateSolidBrush(cGreen);
            HGDIOBJ oldBrush = SelectObject(hdc, dotBrush);
            HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
            Ellipse(hdc, 12, wy + 4, 18, wy + 10);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(dotBrush);

            text(wk.name, 22, wy, 95, 13, fBold, cText,
                 DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
            text(FormatHash(wk.hash30m), 117, wy, 85, 13, fNormal, cText,
                 DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
            text(FormatMoney(wk.profitDaily) + L"/天", 248, wy, 100, 13, fNormal, cGreen,
                 DT_RIGHT | DT_VCENTER | DT_END_ELLIPSIS);

            std::wstring detail = wk.scheme + L" " + wk.coin +
                                  L"  24h " + FormatHash(wk.hash24h) +
                                  L"  有效/过期/无效 " +
                                  std::to_wstring(wk.valid) + L"/" +
                                  std::to_wstring(wk.stale) + L"/" +
                                  std::to_wstring(wk.invalid);
            if (!wk.agent.empty()) detail += L"  " + wk.agent;
            text(detail, 22, wy + 13, 326, 12, fNormal, cMuted,
                 DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);

            wy += 28;
            ++shown;
        }

        if (shown == 0) {
            text(g_haveHardware ? L"暂无在线 worker" : L"硬件数据获取中…",
                 12, 84, 336, 14, fNormal, cMuted,
                 DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
        }
        if (g_online > 8) {
            text(L"还有 " + std::to_wstring(g_online - 8) + L" 个在线 worker…",
                 22, wy, 326, 12, fNormal, cMuted,
                 DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
        }

        DeleteObject(fBold);
        DeleteObject(fNormal);

        if (bits) {
            BYTE* px = static_cast<BYTE*>(bits);
            const bool useBg = (g_bgW == w && g_bgH == h &&
                                g_bg.size() == static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
            for (int i = 0; i < w * h; ++i) {
                BYTE* q = px + i * 4;
                bool foreground;
                if (useBg) {
                    const BYTE* b = g_bg.data() + i * 4;
                    foreground = (q[0] != b[0] || q[1] != b[1] || q[2] != b[2]);
                } else {
                    foreground = (q[0] || q[1] || q[2]);
                }
                q[3] = foreground ? 255 : backgroundAlpha;
            }
        }

        RECT wr;
        GetWindowRect(hwnd, &wr);
        POINT dst{wr.left, wr.top};
        POINT src{0, 0};
        SIZE size{w, h};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(hwnd, screenDc, &dst, &size, hdc, &src, 0, &blend, ULW_ALPHA);
        SetWindowRgn(hwnd, CreateRoundRectRgn(0, 0, w + 1, h + 1, 20, 20), TRUE);

        SelectObject(hdc, oldBmp);
        DeleteObject(dib);
        DeleteDC(hdc);
        ReleaseDC(nullptr, screenDc);

        EndPaint(hwnd, &ps);
        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ToggleOverlay() {
    if (!g_overlayClassRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = OverlayProc;
        wc.hInstance = g_hinst;
        wc.lpszClassName = kOverlayClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        g_overlayClassRegistered = true;
    }

    if (!g_overlay) {
        DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_LAYERED |
                        WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
        g_overlay = CreateWindowExW(exStyle, kOverlayClass, L"Kryptex Balance",
                                    WS_POPUP, 0, 0, 360, 96,
                                    nullptr, nullptr, g_hinst, nullptr);
        if (!g_overlay) return;
    }

    if (!g_overlayVisible) {
        int x, y, w, h;
        GetOverlayRect(x, y, w, h);
        ShowWindow(g_overlay, SW_HIDE);
        PositionOverlay(g_overlay, h);
        Sleep(20);
        CaptureBackdrop(x, y, w, h);
        ShowWindow(g_overlay, SW_SHOWNOACTIVATE);
        g_overlayVisible = true;
        InvalidateRect(g_overlay, nullptr, TRUE);
    } else {
        ShowWindow(g_overlay, SW_HIDE);
        g_overlayVisible = false;
    }
}

static void ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();

    std::wstring line;
    if (g_haveBalance) {
        line = g_btc + L" BTC";
        if (!g_fiat.empty()) line += L"   ≈ " + g_fiat;
    } else {
        line = g_status.empty() ? L"尚未获取余额" : g_status;
    }
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, line.c_str());
    AppendMenuW(menu, MF_STRING, kMenuOverlay, g_overlayVisible ? L"关闭浮窗" : L"浮窗展示");
    AppendMenuW(menu, MF_STRING, kMenuRefresh, L"刷新");
    AppendMenuW(menu, MF_STRING, kMenuExit, L"退出");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_taskbarCreated && g_taskbarCreated != 0) {
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        return 0;
    }

    switch (msg) {
    case WM_TRAY:
        switch (LOWORD(lParam)) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
        case WM_LBUTTONUP:
            ShowTrayMenu();
            break;
        }
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_REFRESH) RefreshAsync();
        return 0;

    case WM_REFRESH_DONE: {
        RefreshResult* r = reinterpret_cast<RefreshResult*>(lParam);
        g_busy = false;
        if (r) {
            if (r->balanceOk) {
                g_btc = r->btc;
                g_fiat = r->fiat;
                g_haveBalance = true;
                g_status = L"运行中";
            } else {
                g_status = r->balanceError;
                if (!g_haveBalance) {
                    g_btc = L"—";
                    g_fiat.clear();
                }
            }
            if (r->hardwareOk) {
                g_workers = std::move(r->workers);
                g_daily = r->daily;
                g_monthly = r->monthly;
                g_prlHash = r->prlHash;
                g_qtcHash = r->qtcHash;
                g_online = r->online;
                g_haveHardware = true;
                g_hardwareError.clear();
            } else {
                g_hardwareError = r->hardwareError;
            }
            delete r;
        }
        UpdateTray();
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kMenuOverlay:
            ToggleOverlay();
            break;
        case kMenuRefresh:
            g_forceBackdrop = true;
            RefreshAsync();
            break;
        case kMenuExit:
            DestroyWindow(hwnd);
            break;
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_REFRESH);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hinst, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\KryptexTraySingleton");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    g_hinst = hinst;
    InitFontFace();

    std::wstring err;
    if (!LoadConfig(g_cfg, err)) {
        MessageBoxW(nullptr, err.c_str(), L"Kryptex Tray - 配置错误", MB_ICONERROR | MB_OK);
        return 1;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hinst;
    wc.lpszClassName = kClassName;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc)) return 1;

    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"Kryptex Tray",
                             WS_POPUP, 0, 0, 0, 0,
                             nullptr, nullptr, hinst, nullptr);
    if (!g_hwnd) return 1;

    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"Kryptex 正在启动…");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    ApplyTimer();
    RefreshAsync();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (mutex) CloseHandle(mutex);
    return static_cast<int>(msg.wParam);
}
