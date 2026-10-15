#include "app/alert.h"

#include <cstdio>

#if defined(_WIN32)
#include <windows.h>
#include <string>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#elif defined(__linux__)
#include <spawn.h>
#include <sys/wait.h>

#include <initializer_list>
extern char** environ;
#endif

namespace mu::app {

void systemAlert(const char* title, const char* text) {
#if defined(_WIN32)
    auto wide = [](const char* s) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
        std::wstring out(size_t(n > 0 ? n : 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), n);
        return out;
    };
    MessageBoxW(nullptr, wide(text).c_str(), wide(title).c_str(), MB_OK | MB_ICONINFORMATION);
#elif defined(__APPLE__)
    CFStringRef head = CFStringCreateWithCString(nullptr, title, kCFStringEncodingUTF8);
    CFStringRef body = CFStringCreateWithCString(nullptr, text, kCFStringEncodingUTF8);
    CFOptionFlags pressed = 0;
    CFUserNotificationDisplayAlert(0, kCFUserNotificationNoteAlertLevel, nullptr, nullptr, nullptr, head, body,
                                   CFSTR("OK"), nullptr, nullptr, &pressed);
    CFRelease(head);
    CFRelease(body);
#elif defined(__linux__)
    // Linux has no one system box: GNOME's zenity, else KDE's kdialog, whichever the desktop has
    // (docs/plans/linux-release.md). Either way stderr and the log have it too.
    std::fprintf(stderr, "%s: %s\n", title, text);
    const char* zenity[] = {"zenity", "--info", "--no-wrap", "--title", title, "--text", text, nullptr};
    const char* kdialog[] = {"kdialog", "--title", title, "--msgbox", text, nullptr};
    for (const char* const* line : {zenity, kdialog}) {
        pid_t child = 0;
        if (posix_spawnp(&child, line[0], nullptr, nullptr, const_cast<char* const*>(line), environ) != 0) continue;
        int status = 0;
        waitpid(child, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) != 127) return;
    }
#else
    std::fprintf(stderr, "%s: %s\n", title, text);
#endif
}

}  // namespace mu::app
