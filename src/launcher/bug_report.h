// A player's bug report sent home: POST /api/bug on the website (web/server.py), the player's
// own words and where he was standing, read on the admins' /bugs page. The game's report window
// (game/ui/report.h) is where it is written; app/modes/play_mode.cpp sends it on a thread of its
// own, as the crash reports go (launcher/crash_upload.h), with the launcher's ticket as its
// Bearer so it is filed under the account.
//
// The user, 2026-10-16: "we need to add buttons report bug and find perfect place where to put
// in on game", then picked an in-game form sent home with where and who, a small HUD icon, and a
// website admin page to read them on.
#pragma once

#include <string>

namespace mu::launcher {

struct BugReport {
    std::string text;       // the player's words
    std::string world;      // "Lorencia"
    int x = 0, y = 0;       // MU's tile
    std::string character;  // his name, "" when the run has none
    std::string kind;       // his class, "Dark Knight"
    int level = 0;
    std::string platform;   // "windows", "macos" or "linux"
    std::string build;      // MU2_BUILD_VERSION
    std::string renderer;   // "Vulkan", "Metal", "Direct3D 11"
    std::string gpu;        // the card bgfx found, as vendor:device when it gives no name
    std::string server;     // host:port, "" offline
};

// Sends one report to `site` ("https://mu-classic.com"). `token` is the launcher's ticket
// ("ticket:secret") or "". Blocks: run it on a thread. True when the site kept it; otherwise
// `why` says, in words fit for the window.
bool sendBug(const std::string& site, const std::string& token, const BugReport& report,
             std::string* why);

}  // namespace mu::launcher
