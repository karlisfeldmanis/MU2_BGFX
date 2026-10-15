// The crash and freeze reports (core/watch.h) sent home: POST /api/crash on the website
// (web/server.py), one report a request, its minidump beside it on Windows. The launcher sends
// what is waiting each time it opens and again a little after Play, so a game that dies while
// starting is heard from even if it never gets far enough to send its own; the game sends at its
// start too, for those who run MU2.exe or MU2.app without the launcher.
//
// A report that landed is renamed <file>.sent and is not sent again. One still waiting is tried
// on the next start; one the site refused is renamed <file>.refused. Reports a month old, and any
// past the first eight of a start, wait.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace mu::launcher {

// The website the game sends to when it was not given one: the launcher's own (engine.h Place).
inline constexpr const char* kCrashSite = "https://mu-classic.com";

struct CrashFrom {
    std::string site;      // "https://mu-classic.com"
    std::string platform;  // "windows" or "macos"
    std::string sender;    // "launcher" or "game": which program is sending, not which crashed
    std::string token;     // the launcher's sign-in, so the report is filed under the account; or ""
    std::string gpu;       // the launcher's probe (gpu.h), when it has one
    std::string driver;
};

// Sends what is waiting in `folders`. Returns how many landed. Blocks: run it on a thread.
int sendCrashes(const std::vector<std::filesystem::path>& folders, const CrashFrom& from);

// Whether the game was started by a signed-in launcher: it was handed a ticket (MU2_TICKET,
// engine.h GameTicket). The game's own reports go without the account; the launcher's carry it.
bool startedByLauncher();

}  // namespace mu::launcher
