// HTTPS by WinHTTP, Windows' own: its TLS and its certificate store, nothing of ours to patch.
// The Mac has https_curl.cpp.
#include "launcher/https.h"
#include "launcher/engine.h"

#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <memory>

namespace mu::launcher {

namespace {

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring out(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
    return out;
}

std::string lastError(const char* what) {
    char line[96];
    std::snprintf(line, sizeof line, "%s: WinHTTP error %lu", what, GetLastError());
    return line;
}

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
};

// One session for the launcher's life: WinHTTP keeps its connections warm inside it.
HINTERNET session() {
    static HINTERNET one = [] {
        HINTERNET h = WinHttpOpen(L"MU2Launcher", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
        // TLS 1.2 and 1.3 said outright: an older Windows 10's WinHTTP offers 1.2 only when asked.
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
        protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
        if (h && !WinHttpSetOption(h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
            protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            WinHttpSetOption(h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
        }
        return h;
    }();
    return one;
}

// The request opened and sent; `status` its answer's. False with `error` said when it could not be.
bool open(const std::string& method, const std::string& url, const std::string& body,
          const std::vector<std::string>& headers, Handle& connect, Handle& req, int& status,
          std::string& error) {
    if (!session()) {
        error = lastError("WinHttpOpen");
        return false;
    }
    std::wstring wurl = wide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof parts;
    wchar_t host[256] = {}, path[2048] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = DWORD(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = DWORD(std::size(path));
    wchar_t extra[2048] = {};
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = DWORD(std::size(extra));
    if (!WinHttpCrackUrl(wurl.c_str(), DWORD(wurl.size()), 0, &parts)) {
        error = lastError("WinHttpCrackUrl");
        return false;
    }
    connect.h = WinHttpConnect(session(), host, parts.nPort, 0);
    if (!connect.h) {
        error = lastError("WinHttpConnect");
        return false;
    }
    const std::wstring target = std::wstring(path) + extra;
    const DWORD secure = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    req.h = WinHttpOpenRequest(connect.h, wide(method).c_str(), target.c_str(), nullptr, WINHTTP_NO_REFERER,
                               WINHTTP_DEFAULT_ACCEPT_TYPES, secure);
    if (!req.h) {
        error = lastError("WinHttpOpenRequest");
        return false;
    }
    // Resolve, connect and send in 15 s each; a minute for an answer's next bytes.
    WinHttpSetTimeouts(req.h, 15000, 15000, 15000, 60000);
    std::wstring lines;
    for (const std::string& h : headers) lines += wide(h) + L"\r\n";
    if (!WinHttpSendRequest(req.h, lines.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : lines.c_str(),
                            lines.empty() ? 0 : DWORD(-1L), body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                            DWORD(body.size()), DWORD(body.size()), 0) ||
        !WinHttpReceiveResponse(req.h, nullptr)) {
        error = lastError("WinHttpSendRequest");
        return false;
    }
    DWORD code = 0, size = sizeof code;
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &code, &size, WINHTTP_NO_HEADER_INDEX);
    status = int(code);
    return true;
}

}  // namespace

Reply request(const std::string& method, const std::string& url, const std::string& body,
              const std::vector<std::string>& headers) {
    Reply reply;
    Handle connect, req;
    if (!open(method, url, body, headers, connect, req, reply.status, reply.error)) return reply;
    char chunk[16384];
    DWORD got = 0;
    while (WinHttpReadData(req.h, chunk, sizeof chunk, &got) && got > 0) reply.body.append(chunk, got);
    return reply;
}

bool download(const std::string& url, const std::filesystem::path& to, bool resume,
              const std::function<void(uint64_t)>& got, const std::atomic<bool>& stop, std::string& error) {
    std::error_code ec;
    const uint64_t have = resume && std::filesystem::exists(to, ec) ? std::filesystem::file_size(to, ec) : 0;
    std::vector<std::string> headers;
    if (have > 0) headers.push_back("Range: bytes=" + std::to_string(have) + "-");
    Handle connect, req;
    int status = 0;
    if (!open("GET", url, {}, headers, connect, req, status, error)) return false;
    if (status != 200 && status != 206) {
        error = "status " + std::to_string(status);
        return false;
    }
    // A 200 to a Range is the whole file again: start it over rather than append it.
    std::FILE* file = _wfopen(to.wstring().c_str(), have > 0 && status == 206 ? L"ab" : L"wb");
    if (!file) {
        error = "cannot write " + utf8Of(to);
        return false;
    }
    std::vector<char> chunk(1 << 16);
    DWORD read = 0;
    bool ok = true;
    while (true) {
        if (stop.load()) {
            error = "stopped";
            ok = false;
            break;
        }
        if (!WinHttpReadData(req.h, chunk.data(), DWORD(chunk.size()), &read)) {
            error = lastError("WinHttpReadData");
            ok = false;
            break;
        }
        if (read == 0) break;
        if (std::fwrite(chunk.data(), 1, read, file) != read) {
            error = "cannot write " + utf8Of(to);
            ok = false;
            break;
        }
        if (got) got(read);
    }
    std::fclose(file);
    return ok;
}

}  // namespace mu::launcher
