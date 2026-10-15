// MU2 on bgfx. The entry point, and deliberately nothing else.
//
// This file was 1663 lines: argument dispatch, a save reader, a preloader with its own thread
// and its spinner, a model-browser list widget, three frame loops wearing one `if` ladder, and
// a teardown. It is `app/` now -- docs/reference/architecture.md has the shape, and app/application.h
// states the division of labour between the Application and a Mode.
//
// See docs/README.md for what any of it is for.
#include "app/application.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "game/roster.h"  // game::passCount, handed to a restart

#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#else
#include <mach-o/dyld.h>
#include <spawn.h>
#include <climits>
extern char** environ;
#endif
#ifdef _WIN32
// A laptop with two GPUs runs a program on the weak one unless the program asks, by exporting
// NvOptimusEnablement and AmdPowerXpressRequestHighPerformance. bgfx.cpp exports both already.
#endif

namespace {

#ifdef MU2_SHIP
// The player's build takes the player's switches and no other (docs/plans/layout.md): the review,
// the bench, the studio and every request that changes a world are a developer's. A refused
// one is said on stderr -- the log is not open yet -- and the run goes on without it.
std::vector<char*> playersOnly(int argc, char** argv) {
    static const char* const kAlone[] = {"--lobby", "--fullscreen", "--windowed", "--vsync",
                                         "--no-vsync", "--remember", "--fps", "--no-fps",
                                         "--metalfx", "--no-metalfx", "--mute", "--render-thread"};
    static const char* const kValued[] = {"--server", "--cap", "--scale", "--graphics",
                                          "--msaa", "--width", "--height", "--renderer"};
    auto among = [](const char* s, const auto& list) {
        for (const char* one : list) if (!std::strcmp(s, one)) return true;
        return false;
    };
    std::vector<char*> kept = {argv[0]};
    for (int i = 1; i < argc; ++i) {
        if (among(argv[i], kAlone)) {
            kept.push_back(argv[i]);
        } else if (among(argv[i], kValued) && i + 1 < argc) {
            kept.push_back(argv[i]);
            kept.push_back(argv[++i]);
        } else if (std::strncmp(argv[i], "-psn_", 5) != 0) {
            std::fprintf(stderr, "MU2: %s is not a switch of the player's build\n", argv[i]);
        }
    }
    return kept;
}
#endif

// Graphics' Apply changed what only a new start takes -- Textures or Renderer (core::Args::restart)
// -- and the run has ended as Exit Game ends it: the same program again, with the switches it was
// started with, so a player's build opens on the character screen with the options it just wrote.
// The sign-in's count goes with it (game::passCount): a pass counted from 1 again would be refused.
void startAgain(char** started) {
    const std::string count = std::to_string(mu::game::passCount());
#ifdef _WIN32
    SetEnvironmentVariableA("MU2_PASS_COUNT", count.c_str());
    wchar_t exe[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return;
    std::wstring line = GetCommandLineW();
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    if (CreateProcessW(exe, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
                       &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
#else
    setenv("MU2_PASS_COUNT", count.c_str(), 1);
    char exe[PATH_MAX] = {};
    uint32_t size = sizeof exe;
    if (_NSGetExecutablePath(exe, &size) != 0) return;
    pid_t child = 0;
    posix_spawn(&child, exe, nullptr, nullptr, started, environ);
#endif
}

}  // namespace

int main(int argc, char** argv) {
    char** const started = argv;  // as the system gave them, for a restart to give again
    // Opened from Finder or the Dock: started by its path inside build/MU2.app, with no
    // switches (or only the process serial number an older macOS adds). It plays as mu play's
    // lobby does. A run through the build/mu2 link is argv[0] "build/mu2" and keeps its own.
#ifdef MU2_SHIP
    std::vector<char*> players = playersOnly(argc, argv);
    argc = int(players.size());
    players.push_back(nullptr);
    argv = players.data();
    const bool bundled = true;
#else
    const bool bundled = std::strstr(argv[0], ".app/Contents/MacOS/") != nullptr;
#endif
    const bool bare = argc == 1 || (argc == 2 && !std::strncmp(argv[1], "-psn_", 5));
    if (bundled && bare) {
        static const char* play[] = {argv[0], "--lobby", "--fullscreen", "--vsync", "--cap",
                                     "180", "--scale", "0.99", "--remember", nullptr};
        argc = sizeof(play) / sizeof(play[0]) - 1;
        argv = const_cast<char**>(play);
    }
#ifdef _WIN32
    // Windows' sleeps and waits are 15.6 ms coarse by default, which a 60 fps frame and the
    // realm's tick cannot live with: 1 ms while the game runs.
    timeBeginPeriod(1);
#endif
    int code = 0;
    bool again = false;
    {
        mu::app::Application application;
        code = application.run(argc, argv);
        again = application.restartAsked();
    }
    if (again) startAgain(started);
#ifdef _WIN32
    timeEndPeriod(1);
#endif
#if defined(_WIN32) && defined(MU2_SHIP)
    // Out without the static destructors (the Windows audit, 2026-10-15). The crash reports go
    // home on a detached thread (Application::run) that can be inside WinHTTP or the log when
    // the player quits; exit() would destroy the log's mutex and buffer under it, a fault after
    // the game had closed cleanly, filed as a crash on the next start. Everything that matters
    // is shut down and the log closed by now; a report still going is cut and sent next time,
    // since only one that landed is marked sent.
    std::fflush(nullptr);
    std::_Exit(code);
#endif
    return code;
}
