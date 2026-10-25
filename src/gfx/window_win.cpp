// The Mac's two Apple-only pieces of gfx, as Windows has them (docs/plans/windows-client.md):
// no menu bar or Dock to hide in fullscreen, and no MetalFX, so a world drawn at `--scale` under
// 1 reaches the screen through the present's own stretch.
#include "gfx/metalfx.h"
#include "gfx/window.h"

#include <windows.h>

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>  // GLFW_EXPOSE_NATIVE_WIN32 comes from CMake

#include <string>

namespace mu::gfx {

void Window::setPresentation(bool) {}
float Window::notchPoints() { return 0.0f; }

namespace {

// One window, so one: the hook's call carries no pointer of ours.
Window* g_heldWindow = nullptr;
HHOOK g_heldHook = nullptr;
constexpr UINT_PTR kHeldTimer = 0x4d55;

// The system's move, size and menu loops hand each message they take to this thread's
// message filter (CallMsgFilter, MSGF_SIZE / MSGF_MENU) before acting on it, which is what
// reaches the game while they hold the thread. The timer's message is taken here and not
// passed on: Wine's move loop ends on a WM_TIMER it dispatches, and nothing else wants ours.
// glfwPollEvents' own loop calls no filter, so while frames run the timer is ignored.
LRESULT CALLBACK onHeldFilter(int code, WPARAM wparam, LPARAM lparam) {
    const MSG* msg = reinterpret_cast<const MSG*>(lparam);
    if (code >= 0 && msg && msg->message == WM_TIMER && msg->wParam == kHeldTimer) {
        if (g_heldWindow) g_heldWindow->heldTick();
        return 1;
    }
    return CallNextHookEx(g_heldHook, code, wparam, lparam);
}

}  // namespace

void Window::armHeld(bool on) {
    HWND hwnd = glfwGetWin32Window(handle_);
    if (!hwnd) return;
    if (on) {
        g_heldWindow = this;
        SetTimer(hwnd, kHeldTimer, 100, nullptr);
        if (!g_heldHook) g_heldHook = SetWindowsHookExW(WH_MSGFILTER, onHeldFilter, nullptr, GetCurrentThreadId());
    } else {
        KillTimer(hwnd, kHeldTimer);
        if (g_heldHook) UnhookWindowsHookEx(g_heldHook);
        g_heldHook = nullptr;
        g_heldWindow = nullptr;
    }
}

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
