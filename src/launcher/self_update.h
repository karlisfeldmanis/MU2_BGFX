// The launcher's own update (docs/plans/launcher.md, L3): before anything else at its start it asks
// the site for the newest launcher, /game/launcher-<platform>/manifest.json (tools/mu/release.py
// `mu release launcher`), and replaces itself when its own program is not that one.
//
// Robust by construction:
//   - Which launcher it is, is the SHA-256 of its own program file, not a version it was told: a
//     launcher that has updated matches, and never updates again in a loop.
//   - Nothing is half done: the new one is downloaded beside it and its hash checked, then swapped
//     in by renames (Windows renames a running exe; the Mac swaps the whole MU2Launcher.app), and
//     started. If it will not start, the old one is renamed back.
//   - A release that fails twice is not tried again (launcher-update.json in the user folder), so
//     a bad launcher on the site cannot hold a player in a restart loop.
//   - No site, no write access to its own folder, or a developer's build (out/build, or
//     --no-self-update): it carries on as it is.
#pragma once

#include <string>
#include <vector>

#include "launcher/engine.h"

namespace mu::launcher {

enum class SelfUpdate {
    Current,  // this is the newest launcher
    Started,  // the newer one is in place and running: this one should close now
    Skipped,  // not updated, for the reason in `note`; carry on
};

// Blocks (a download of a few MB): run it on the worker. `args` are handed to the new launcher.
SelfUpdate selfUpdate(const Place& place, const std::vector<std::string>& args, std::string& note);

// Whether this launcher is the site's newest, without changing anything (--check's line).
SelfUpdate launcherCurrent(const Place& place, std::string& note);

// This launcher's own program file, and whether it is a developer's (built in out/), which is
// never replaced by the site's and never moved into an install (engine.h placeLauncher).
fs::path ownProgram();
bool developers(const fs::path& program);

// What a swap left behind: the old program, the staged download. Called at every start.
void cleanAfterSelfUpdate();

}  // namespace mu::launcher
