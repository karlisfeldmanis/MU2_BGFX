#include "core/paths.h"

#ifdef MU2_SHIP
#include <mach-o/dyld.h>
#include <unistd.h>

#include <climits>
#include <cstdlib>
#include <filesystem>
#endif

namespace mu::core {

#ifdef MU2_SHIP

namespace {

// The bundle's Contents/Resources, from where the executable is: MU2.app/Contents/MacOS/MU2.
std::string resources() {
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) return ".";
    char real[PATH_MAX];
    const std::string exe = ::realpath(path, real) ? real : path;
    return std::filesystem::path(exe).parent_path().parent_path().string() + "/Resources";
}

// The player's own folder, where an installed app writes: it must not write inside itself.
std::string userFolder() {
    const char* home = std::getenv("HOME");
    const std::string folder =
        std::string(home ? home : ".") + "/Library/Application Support/MU2";
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    return folder;
}

// logs/ in the client's folder, the one MU2.app sits in: the log and the crash and freeze
// reports, where a player who is asked for them finds them (the user, 2026-10-13: "we need to
// save logs and crash reports inside client folder in folder /logs"). Where that folder cannot
// be written -- a downloaded app macOS runs from a read-only copy until it is moved
// (AppTranslocation), or an /Applications the player may not write -- the player's own folder.
std::string logsFolder(const std::string& resources, const std::string& user) {
    const std::string client =
        std::filesystem::path(resources).parent_path().parent_path().parent_path().string();
    if (client.find("/AppTranslocation/") == std::string::npos) {
        const std::string logs = client + "/logs";
        std::error_code error;
        std::filesystem::create_directories(logs, error);
        if (!error && ::access(logs.c_str(), W_OK) == 0) return logs;
    }
    const std::string logs = user + "/logs";
    std::error_code error;
    std::filesystem::create_directories(logs, error);
    return logs;
}

}  // namespace

const Folders& folders() {
    // The player's build: the data in the bundle's packs, and nothing of the checkout. The
    // studio's folders are empty, because the studio is not in this build.
    static const Folders folders = [] {
        Folders f;
        f.root = resources();
        f.data = f.root + "/data";
        f.assets = f.data + "/assets";
        f.sheets = f.data + "/sheets";
        f.shaders = f.data + "/shaders/metal";
        f.fonts = f.data + "/fonts";
        f.user = userFolder();
        f.shots = f.user + "/shots";
        f.crashes = logsFolder(f.root, f.user);
        f.log = f.crashes + "/mu2.log";
        f.run = f.user;
        return f;
    }();
    return folders;
}

#else

const Folders& folders() {
    static const Folders folders = {
        MU2_ROOT_DIR,
        "",
        MU2_ASSET_DIR,
        MU2_SHEET_DIR,
        MU2_SHADER_DIR,
        MU2_FONT_DIR,
        MU2_CONTENT_DIR,
        MU2_WORK_DIR,
        MU2_USER_DIR,
        MU2_SHOT_DIR,
        MU2_CRASH_DIR,
        MU2_LOG_FILE,
        MU2_RUN_DIR,
    };
    return folders;
}

#endif

}  // namespace mu::core
