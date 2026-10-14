// HTTPS for the launcher: the website's sign-in (web/server.py) and the game's files
// (tools/mu/release.py). Windows' own WinHTTP there (https_win.cpp), the system's libcurl on the
// Mac (https_curl.cpp), so neither carries a TLS library of ours. Calls block; the launcher makes
// them on a worker thread.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace mu::launcher {

// Who asks, as the site hears it. The number is the launcher's build: the site turns away the
// launchers that said plain "MU2Launcher" (web/server.py OLD_LAUNCHER), which cannot update
// themselves, with the words to download the new one. Raised when a launcher must be replaced.
inline constexpr const char* kAgent = "MU2Launcher/2";

struct Reply {
    int status = 0;        // the HTTP status, 0 when nothing came back
    std::string body;
    std::string error;     // why nothing came back, for the log; never shown as it is
};

// One request. `headers` are whole lines ("Authorization: Bearer ...").
Reply request(const std::string& method, const std::string& url, const std::string& body = {},
              const std::vector<std::string>& headers = {});

// A file to `to`, resumed from what is already there (a Range request) when `resume` is set.
// `got` hears the bytes as they land; `stop` set from another thread ends it early. True when the
// whole file is there.
bool download(const std::string& url, const std::filesystem::path& to, bool resume,
              const std::function<void(uint64_t)>& got, const std::atomic<bool>& stop, std::string& error);

}  // namespace mu::launcher
