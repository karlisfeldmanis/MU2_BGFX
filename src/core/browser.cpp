#include "core/browser.h"

#include <thread>

#include "core/log.h"

#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace mu::core {

void openInBrowser(const std::string& url) {
#ifdef _WIN32
    // The shell can take a moment to find the browser, so it is asked on a thread of its own,
    // with COM up on it as ShellExecute wants.
    std::thread([url] {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        const std::wstring wide(url.begin(), url.end());
        const auto done = reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (done <= 32) logf("browser: could not open %s (%d)", url.c_str(), int(done));
        if (SUCCEEDED(com)) CoUninitialize();
    }).detach();
#else
#ifdef __linux__
    std::string open = "xdg-open";  // the desktop's own browser
#else
    std::string open = "/usr/bin/open";
#endif
    std::string target = url;
    char* argv[] = {open.data(), target.data(), nullptr};
    pid_t pid = 0;
    if (posix_spawnp(&pid, open.c_str(), nullptr, nullptr, argv, environ) != 0) {
        logf("browser: could not start %s for %s", open.c_str(), url.c_str());
        return;
    }
    // Reaped on a thread of its own, so the opener leaves no zombie and nothing waits on it.
    std::thread([pid] {
        int status = 0;
        waitpid(pid, &status, 0);
    }).detach();
#endif
}

}  // namespace mu::core
