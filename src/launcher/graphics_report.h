// What the game draws with, told to the website: POST /api/graphics (web/server.py), under the
// launcher's ticket, and shown on the admins' /online page in place of the launcher's first guess
// (its machine report's preset, which the character screen's measure and the player's own rows
// change after). app/application.cpp sends it on a thread of its own once the rows have held still
// a few seconds after a change, and again every ten minutes for the frame rate.
//
// The user, 2026-10-23: "we need to see on web what setting he is actualy playing".
#pragma once

#include <string>

namespace mu::launcher {

struct GraphicsReport {
    std::string preset;    // "High" ... "Lowest", or "Custom" (core::graphicsName)
    std::string renderer;  // "Vulkan", "Metal", "Direct3D 11"
    int scale = 100;       // percent
    int msaa = 1;
    int shadows = 0;       // -1 off, 0..2 Low..High
    bool ssao = false, bloom = false, reflections = false, lights = false;
    int grass = 0;
    int textures = 1;      // as this run loaded them: 0 half, 1 full
    int cap = 0;           // frames a second, 0 free
    int fps = 0;           // the last second's
    bool tuned = false;    // measured on the character screen
    bool hand = false;     // a row set by hand
    int width = 0, height = 0;
    bool fullscreen = false;
};

// Sends one report to `site`. `token` is the launcher's ticket ("ticket:secret"). Blocks: run it on
// a thread. True when the site kept it.
bool sendGraphics(const std::string& site, const std::string& token, const GraphicsReport& report);

}  // namespace mu::launcher
