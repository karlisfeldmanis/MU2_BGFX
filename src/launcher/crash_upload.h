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
    std::string token;     // the launcher's sign-in or the game's ticket, so the report is filed under the account; or ""
    std::string gpu;       // the launcher's probe (gpu.h), when it has one
    std::string driver;
};

// **The logs sent home** (the user, 2026-10-15: "we need to send also game launching logs to
// server so we can see the problems like it was for Leo"). A digest of the launcher's log
// `launcherLog` and the newest mu2.log in `gameLogs`: each one's head, the lines that say what
// went wrong (the window and its focus, the GPU and the targets, freezes, errors, the launcher's
// moves and updates), and its last lines. Written as log-<when>.txt in `into`, one of the folders
// sendCrashes reads, so it goes home as a report does and is kept beside the crashes (web/server.py,
// kind "log"). Nothing is written when neither log changed since the last digest.
void writeLogDigest(const std::vector<std::filesystem::path>& gameLogs, const std::filesystem::path& launcherLog,
                    const std::filesystem::path& into);

// Sends what is waiting in `folders`. Returns how many landed. Blocks: run it on a thread.
int sendCrashes(const std::vector<std::filesystem::path>& folders, const CrashFrom& from);

// The ticket a signed-in launcher handed the game (MU2_TICKET, engine.h GameTicket), as
// "ticket:secret", or "" when it was not started so. Sent as the game's own reports' Bearer, over
// HTTPS only, which files them under the account (web/server.py ticket_account).
std::string ticketFromEnvironment();
inline bool startedByLauncher() { return !ticketFromEnvironment().empty(); }

}  // namespace mu::launcher
