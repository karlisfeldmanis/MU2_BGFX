// The Mac's two Apple-only pieces of gfx, as Windows has them (docs/plans/windows-client.md):
// no menu bar or Dock to hide in fullscreen, and no MetalFX, so a world drawn at `--scale` under
// 1 reaches the screen through the present's own stretch.
#include "gfx/metalfx.h"
#include "gfx/window.h"

#include <windows.h>

#include <string>

namespace mu::gfx {

void Window::setPresentation(bool) {}

std::string Window::focusOwner() {
    HWND front = GetForegroundWindow();
    if (!front) return "nothing";
    wchar_t title[256] = {};
    GetWindowTextW(front, title, 256);
    DWORD pid = 0;
    GetWindowThreadProcessId(front, &pid);
    wchar_t exe[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        QueryFullProcessImageNameW(process, 0, exe, &size);
        CloseHandle(process);
    }
    const auto narrow = [](const wchar_t* w) {
        char out[512] = {};
        WideCharToMultiByte(CP_UTF8, 0, w, -1, out, sizeof out - 1, nullptr, nullptr);
        return std::string(out);
    };
    std::string program = narrow(exe);
    const size_t slash = program.find_last_of("\\/");
    if (slash != std::string::npos) program = program.substr(slash + 1);
    return "\"" + narrow(title) + "\" (" + (program.empty() ? "pid " + std::to_string(pid) : program) + ")";
}

namespace metalfx {

bool supported() { return false; }
void attach(bgfx::ViewId, bgfx::TextureHandle, bgfx::TextureHandle) {}
void detach() {}

}  // namespace metalfx
}  // namespace mu::gfx
