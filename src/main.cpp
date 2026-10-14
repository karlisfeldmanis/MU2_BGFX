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
#include <cstring>
#include <vector>

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
                                          "--msaa", "--width", "--height"};
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

}  // namespace

int main(int argc, char** argv) {
    // Opened from Finder or the Dock: started by its path inside build/MU2.app, with no
    // switches (or only the process serial number an older macOS adds). It plays as main.sh's
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
    mu::app::Application application;
    return application.run(argc, argv);
}
