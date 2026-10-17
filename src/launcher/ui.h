// MU2Launcher's screens (docs/plans/launcher.md, L3; the "MU2 Launcher" canvas): Sign in, then
// one screen that installs, updates and plays. 960×540, the game's own buttons and colours.
#pragma once

#include <string>
#include <vector>

#include "launcher/engine.h"

namespace mu::launcher {

struct UiOptions {
    Place place;
    // --shot FILE: a hidden window, the screen named by `shotScreen` drawn and saved, then out.
    std::string shot;
    std::string shotScreen;  // signin, error, install, downloading, ready, regions, driver
    // --renderer vulkan|d3d11 (Windows): what its window draws with, over the probe's pick
    // (gpu.h rendererFor). -1 is the probe's.
    int renderer = -1;
    // --no-self-update: this launcher stays as it is (launcher/self_update.h).
    bool selfUpdate = true;
    // --server HOST[:PORT]: the game Play starts goes there, over the region's -- a test's own
    // server beside its own --site, so a Wine run never reaches a live one.
    std::string server;
    // What it was started with, handed on to a newer launcher that takes its place.
    std::vector<std::string> args;
};

int runUi(const UiOptions& options);

}  // namespace mu::launcher
