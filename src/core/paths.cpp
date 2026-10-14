#include "core/paths.h"

#if defined(MU2_SHIP) && defined(_WIN32)
#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <filesystem>
#elif defined(MU2_SHIP)
#include <mach-o/dyld.h>
#include <unistd.h>

#include <climits>
#include <cstdlib>
#include <filesystem>
#endif

namespace mu::core {

#if defined(MU2_SHIP) && defined(_WIN32)

namespace {

// Windows (docs/plans/windows-client.md): wide names from the system, UTF-8 strings in the game,
// so a player called Jānis keeps the ā. The exe's manifest makes the "A" calls and fopen read
// UTF-8 (src/app/mu2.manifest).
std::string utf8(const wchar_t* wide) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(size_t(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), n, nullptr, nullptr);
    return out;
}

// The client's folder: the one MU2.exe is in, with data/ and logs/ beside it.
std::string clientFolder() {
    wchar_t exe[32768];
    const DWORD n = GetModuleFileNameW(nullptr, exe, DWORD(sizeof(exe) / sizeof(exe[0])));
    if (n == 0 || n >= sizeof(exe) / sizeof(exe[0])) return ".";
    const std::u8string folder = std::filesystem::path(exe).parent_path().generic_u8string();
    return std::string(folder.begin(), folder.end());
}

// A UTF-8 string as a path: std::filesystem would read a plain string in the ANSI code page.
std::filesystem::path pathOf(const std::string& utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

// %APPDATA%\MU2: the player's settings, saves and shots.
std::string userFolder() {
    std::string folder;
    PWSTR roaming = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming) == S_OK) {
        folder = utf8(roaming) + "/MU2";
    }
    CoTaskMemFree(roaming);
    if (folder.empty()) folder = clientFolder() + "/user";
    std::error_code error;
    std::filesystem::create_directories(pathOf(folder), error);
    return folder;
}

// logs/ in the client's folder, as on the Mac; where Windows will not let it be written there
// (an install under Program Files), the player's own folder.
std::string logsFolder(const std::string& client, const std::string& user) {
    for (const std::string& logs : {client + "/logs", user + "/logs"}) {
        std::error_code error;
        std::filesystem::create_directories(pathOf(logs), error);
        const std::string probe = logs + "/.write";
        if (FILE* f = std::fopen(probe.c_str(), "w")) {
            std::fclose(f);
            std::remove(probe.c_str());
            return logs;
        }
    }
    return user;
}

}  // namespace

const Folders& folders() {
    static const Folders folders = [] {
        Folders f;
        f.root = clientFolder();
        f.data = f.root + "/data";
        f.assets = f.data + "/assets";
        f.sheets = f.data + "/sheets";
        f.shaders = f.data + "/shaders/spirv";
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

#elif defined(MU2_SHIP)

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
