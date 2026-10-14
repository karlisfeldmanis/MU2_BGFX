// MU2Launcher's screens (docs/plans/launcher.md, L3; the "MU2 Launcher" canvas): Sign in, then
// one screen that installs, updates and plays. 960×540, the game's own buttons and colours.
#pragma once

#include <string>

#include "launcher/engine.h"

namespace mu::launcher {

struct UiOptions {
    Place place;
    // --shot FILE: a hidden window, the screen named by `shotScreen` drawn and saved, then out.
    std::string shot;
    std::string shotScreen;  // signin, error, install, downloading, ready
};

int runUi(const UiOptions& options);

}  // namespace mu::launcher
