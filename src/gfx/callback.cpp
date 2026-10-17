#include "gfx/callback.h"

#include <bimg/encode.h>
#include <bx/file.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32) && defined(MU2_SHIP)
#include <windows.h>

#include <thread>
#endif

#include "core/args.h"
#include "core/log.h"
#include "core/watch.h"

namespace mu::gfx {
namespace {

bool (*g_startAgain)() = nullptr;

// After a lost device the next start draws less. Salvi's Iris Plus G7, 2026-10-15: Low took
// 55-63 ms a frame in Lorencia's rain, then one frame ran 3 s and Windows reset the driver
// (TDR). options.txt is rewritten one preset lighter than the one its scale sits in -- the
// Graphics page's keys only, every other line kept -- so a card is not asked for that frame
// again, and a second loss steps once more. Each key keeps the lighter of what it had and what
// the preset has (every key is heavier the higher it is), so the launcher's low-end set, whole
// scale but its effects off (core::kLowEndFirstRun), steps to 85% and not to Medium's MSAA,
// SSAO and bloom. Returns the preset's name, or null where there is no options.txt (a run that
// does not --remember).
const char* lightenOptions() {
    const std::string path = core::optionsPath();
    FILE* file = std::fopen(path.c_str(), "r");
    if (!file) return nullptr;
    std::vector<std::pair<std::string, int>> lines;
    char key[32];
    int value = 0, scale = 100;
    while (std::fscanf(file, "%31s %d", key, &value) == 2) {
        lines.emplace_back(key, value);
        if (!std::strcmp(key, "scale")) scale = value;
    }
    std::fclose(file);

    int at = core::kGraphicsPresetCount - 1;
    for (int i = 0; i < core::kGraphicsPresetCount; ++i)
        if (core::kGraphicsPresets[i].scale >= scale) {
            at = i;
            break;
        }
    const core::GraphicsPreset& to = core::kGraphicsPresets[at > 0 ? at - 1 : 0];
    const std::pair<const char*, int> graphics[] = {
        {"scale", to.scale}, {"msaa", to.msaa},         {"shadows", to.shadows},
        {"ssao", to.ssao},   {"bloom", to.bloom},       {"reflections", to.reflections},
        {"grass", to.grass}, {"lights", to.lights},     {"textures", to.textures},
    };
    for (const auto& [name, set] : graphics) {
        bool found = false;
        for (auto& [k, v] : lines)
            if (k == name) v = std::min(v, set), found = true;
        if (!found) lines.emplace_back(name, set);
    }
    // And held to kLowEndCap frames a second: a step down the presets barely moves a card that
    // drew 4 ms frames free at 150 fps. Gordichan's GTX 1650 laptop, 2026-10-17, lost its device
    // every 2-4 minutes on Medium as on High; what it was asked for was the rate, not the frame.
    // 0 is free here, so it is the cap's own rule, not the min the keys above take.
    bool capped = false;
    for (auto& [k, v] : lines)
        if (k == "cap") v = v > 0 ? std::min(v, core::kLowEndCap) : core::kLowEndCap, capped = true;
    if (!capped) lines.emplace_back("cap", core::kLowEndCap);
    // And NVIDIA off Direct3D 11, to Auto, which is Vulkan: every NVIDIA device lost in the
    // players' reports to 2026-10-17 was on Direct3D 11, and none on Vulkan since the acquire
    // patch (launcher/gpu.h rendererFor). Auto still falls back if Vulkan does not start.
    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps && caps->rendererType == bgfx::RendererType::Direct3D11 && caps->vendorId == BGFX_PCI_ID_NVIDIA) {
        for (auto& [k, v] : lines)
            if (k == "renderer") v = 0;
        core::logf("graphics: NVIDIA on Direct3D 11 lost its device; the next start is on Auto (Vulkan)");
    }

    file = std::fopen(path.c_str(), "w");
    if (!file) return nullptr;
    for (const auto& [k, v] : lines) std::fprintf(file, "%s %d\n", k.c_str(), v);
    std::fclose(file);
    return to.name;
}

}  // namespace

void setStartAgain(bool (*again)()) { g_startAgain = again; }

void Callback::fatal(const char* filePath, uint16_t line, bgfx::Fatal::Enum code, const char* str) {
    core::logError("bgfx fatal %d at %s:%u: %s", int(code), filePath, unsigned(line), str);
    const char* lighter = code == bgfx::Fatal::DeviceLost ? lightenOptions() : nullptr;
    if (lighter) core::logf("graphics: the next start opens on %s, after the lost device", lighter);
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
    // The report is filed for the next start to send; the game itself comes back on its own
    // (main.cpp startAgainAfterLoss), so the player sees a load and not an error box.
    if (code == bgfx::Fatal::DeviceLost && g_startAgain && g_startAgain()) {
        core::logf("graphics: the device was lost; MU2 starts again");
        // Paused, not stopped: watchStop joins the freeze watch, which may be inside its own
        // dump of this process, the driver's threads suspended under it mid-reset -- the old
        // game then never ends, still on the server, and turns the new one's rejoin away as
        // "already playing". The process's end takes the watch with it.
        core::watchPause();
        core::logClose();
#if defined(_WIN32) && defined(MU2_SHIP)
        // Not ExitProcess: its DLL detach would call into the driver that has just stopped.
        TerminateProcess(GetCurrentProcess(), 3);
#endif
        std::_Exit(3);
    }
#if defined(_WIN32) && defined(MU2_SHIP)
    // The player is told, rather than left looking at a game that vanished (the GPU audit,
    // 2026-10-15): a lost device is a driver reset (TDR), a driver update or a card that
    // stopped. The freeze watch is paused first, since the box holds the frame -- paused, not
    // joined, for the reason above; then the process ends without abort(), whose fast fail
    // would put Windows' own error box after ours.
    core::watchPause();
    core::logClose();
    const wchar_t* text =
        code != bgfx::Fatal::DeviceLost
            ? L"MU2's graphics stopped with an error.\n\nStart MU2 again; if it happens again, "
              L"update your graphics card's driver."
        : lighter
            ? L"MU2's graphics stopped: the graphics card or its driver stopped answering, most "
              L"often because a frame asked more of the card than it could draw in time.\n\n"
              L"MU2 will start with lighter graphics next time (Options, Graphics). If it happens "
              L"again, update your graphics card's driver (NVIDIA, AMD or Intel, from their site) "
              L"and close programs that draw over games (recording or overlay tools)."
            : L"MU2's graphics stopped: the graphics card or its driver stopped answering.\n\n"
              L"Update your graphics card's driver (NVIDIA, AMD or Intel, from their site) and "
              L"start MU2 again. If it happens again, close programs that draw over games "
              L"(recording or overlay tools) and try once more.";
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
