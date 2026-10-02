// MU2 on bgfx. The entry point, and deliberately nothing else.
//
// This file was 1663 lines: argument dispatch, a save reader, a preloader with its own thread
// and its spinner, a model-browser list widget, three frame loops wearing one `if` ladder, and
// a teardown. It is `app/` now -- docs/architecture.md has the shape, and app/application.h
// states the division of labour between the Application and a Mode.
//
// See PLAN.md and docs/sprints/ for what any of it is for.
#include "app/application.h"

#include <cstring>

int main(int argc, char** argv) {
    // Opened from Finder or the Dock: started by its path inside build/MU2.app, with no
    // switches (or only the process serial number an older macOS adds). It plays as main.sh's
    // lobby does. A run through the build/mu2 link is argv[0] "build/mu2" and keeps its own.
    const bool bundled = std::strstr(argv[0], ".app/Contents/MacOS/") != nullptr;
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
