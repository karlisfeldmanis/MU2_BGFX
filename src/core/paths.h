#pragma once

#include <string>

namespace mu::core {

// Every folder the game reads or writes, decided in one place (docs/plans/layout.md). A developer's
// build finds them where CMake's MU_DIRS says the checkout keeps them; the player's build will
// find its data beside the executable and write into the player's own user folder. Nothing else
// in src/ names a top-level folder.
struct Folders {
    std::string root;     // the checkout; in the player's build, the bundle's Resources
    std::string data;     // the player's build only: where its packs are (core/pack.h)
    std::string assets;   // the cook's output
    std::string sheets;   // lighting, the times of day, the lobby, the worlds' light
    std::string shaders;  // the compiled Metal binaries
    std::string fonts;    // the .ttf faces mu bootstrap fetches
    std::string content;  // what we author: the recipes the studio edits
    std::string work;     // the cook's workshop: the studio's sockets, journal and logs
    std::string user;     // options, characters, layouts, account.key
    std::string shots;    // --shot's pictures, and the studio's swaps
    std::string crashes;  // a crash's or a freeze's report (core/watch.h); the player's: logs/
    std::string log;      // the run's log, a file: mu2.log, and mu2.log.previous beside it
    std::string run;      // a developer run's other outputs: the hunt log
};

const Folders& folders();

}  // namespace mu::core
