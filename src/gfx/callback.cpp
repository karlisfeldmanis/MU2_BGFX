#include "gfx/callback.h"

#include <bimg/encode.h>
#include <bx/file.h>

#include <cstdio>

#if defined(_WIN32) && defined(MU2_SHIP)
#include <windows.h>

#include <thread>
#endif

#include "core/log.h"
#include "core/watch.h"

namespace mu::gfx {

void Callback::fatal(const char* filePath, uint16_t line, bgfx::Fatal::Enum code, const char* str) {
    core::logError("bgfx fatal %d at %s:%u: %s", int(code), filePath, unsigned(line), str);
    // Into a crash report before the abort, which on Windows reaches no handler (core/watch.h):
    // a device lost -- the driver's own crash or a GPU timeout -- read as one, sent home.
    const char* name = code == bgfx::Fatal::DeviceLost             ? "DeviceLost (the GPU or its driver stopped)"
                       : code == bgfx::Fatal::UnableToInitialize    ? "UnableToInitialize"
                       : code == bgfx::Fatal::UnableToCreateTexture ? "UnableToCreateTexture"
                       : code == bgfx::Fatal::InvalidShader         ? "InvalidShader"
                                                                    : "DebugCheck";
    char what[600];
    std::snprintf(what, sizeof(what), "bgfx fatal %s at %s:%u: %s", name, filePath, unsigned(line), str);
    core::watchReport(what);
#if defined(_WIN32) && defined(MU2_SHIP)
    // The player is told, rather than left looking at a game that vanished (the GPU audit,
    // 2026-10-15): a lost device is a driver reset (TDR), a driver update or a card that
    // stopped. The freeze watch stops first, since the box holds the frame; then the process
    // ends without abort(), whose fast fail would put Windows' own error box after ours.
    core::watchStop();
    core::logClose();
    const wchar_t* text =
        code == bgfx::Fatal::DeviceLost
            ? L"MU2's graphics stopped: the graphics card or its driver stopped answering.\n\n"
              L"Update your graphics card's driver (NVIDIA, AMD or Intel, from their site) and "
              L"start MU2 again. If it happens again, close programs that draw over games "
              L"(recording or overlay tools) and try once more."
            : L"MU2's graphics stopped with an error.\n\nStart MU2 again; if it happens again, "
              L"update your graphics card's driver.";
    // The game's own windows hidden first, so the box is not under a borderless fullscreen one;
    // then the box on a thread of its own, whose queue neither GLFW's window nor its hold timer
    // (gfx/window_win.cpp) reaches. Not MB_TOPMOST or MB_SETFOREGROUND: under Wine those boxes
    // close themselves after a few seconds, as if OK were pressed.
    EnumWindows(
        [](HWND window, LPARAM) -> BOOL {
            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            if (pid == GetCurrentProcessId()) ShowWindow(window, SW_HIDE);
            return TRUE;
        },
        0);
    std::thread([text] { MessageBoxW(nullptr, text, L"MU2", MB_OK | MB_ICONERROR); }).join();
    TerminateProcess(GetCurrentProcess(), 3);
#endif
    core::logClose();
    abort();
}

void Callback::traceVargs(const char* filePath, uint16_t line, const char* format, va_list argList) {
    char body[2048];
    bx::vsnprintf(body, sizeof(body), format, argList);
    // bgfx ends most of its traces with a newline; the log adds its own.
    size_t n = size_t(bx::strLen(body));
    while (n && (body[n - 1] == '\n' || body[n - 1] == '\r')) body[--n] = '\0';
    if (n) core::logf("bgfx %s:%u %s", filePath, unsigned(line), body);
}

void Callback::screenShot(const char* filePath, uint32_t width, uint32_t height, uint32_t pitch,
                          bgfx::TextureFormat::Enum format, const void* data, uint32_t size,
                          bool yflip) {
    bx::FileWriter writer;
    bx::Error err;
    if (!bx::open(&writer, filePath, false, &err)) {
        core::logError("cannot write the shot %s", filePath);
        return;
    }
    // The format is bgfx's to say, not ours to assume: the backbuffer is BGRA8 here and
    // need not be on the next machine.
    bimg::imageWritePng(&writer, width, height, pitch, data, bimg::TextureFormat::Enum(format),
                        yflip, &err);
    bx::close(&writer);
    if (!err.isOk()) {
        core::logError("the shot %s did not encode", filePath);
        return;
    }
    core::logf("shot %s (%ux%u, %u bytes)", filePath, width, height, size);
}

}  // namespace mu::gfx
