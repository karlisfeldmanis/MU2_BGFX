// What the launcher does, without its screens (docs/plans/launcher.md, L3): signs in on the website,
// keeps the token, reads the release's manifest, brings the installed game up to it file by file,
// and starts MU2.exe signed in. Each call blocks; the screens make them on a worker.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mu::launcher {

namespace fs = std::filesystem;

// The site (web/) and the platform its releases are under (tools/mu/release.py).
struct Place {
    std::string site = "https://mu-classic.com";
    // Where the player's links go (Create account, Forgot password, Edit account): always the
    // public website, even when `site` is a local one for a test. Launchers built before the
    // domain (2026-10-14) carry https://37.27.158.226 here, which Caddy still answers.
    std::string web = "https://mu-classic.com";
#ifdef _WIN32
    std::string platform = "windows";
#elif defined(__linux__)
    std::string platform = "linux";
#else
    std::string platform = "macos";
#endif
};

// Paths to and from UTF-8, Windows' wide names and all (a user folder named Jānis).
inline fs::path pathOf(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }
inline std::string utf8Of(const fs::path& path) {
    const std::u8string u = path.u8string();
    return std::string(u.begin(), u.end());
}

// ---- the account --------------------------------------------------------------------------------

struct Signed {
    std::string token;  // 64 hex digits, the game's account key while it lasts (90 days)
    std::string email;
};

enum class Answer { Ok, Wrong, Busy, Offline };

// POST /api/login. `message` is the website's own words for a refusal, fit to show.
Answer signIn(const Place& place, const std::string& email, const std::string& password, Signed& out,
              std::string& message);
// GET /api/me: Ok while the token is good, Wrong once the website no longer keeps it.
Answer stillSigned(const Place& place, const Signed& who);
// What the game is handed in place of the token (net/pass.h, docs/plans/security.md): a ticket the
// server may ask about once, and the secret the game proves itself with on its line.
struct GameTicket {
    std::string ticket;  // 32 hex digits
    std::string secret;  // 64 hex digits
};
// POST /api/ticket, at each Play. Wrong once the website no longer keeps the token.
Answer gameTicket(const Place& place, const Signed& who, GameTicket& out, std::string& message);
void signOut(const Place& place, const Signed& who);

// The token kept between runs: DPAPI-sealed on Windows, so only this Windows user can open it;
// a file only its owner reads elsewhere.
bool keep(const Signed& who);
bool kept(Signed& who);
void forget();

// ---- the launcher's own files -------------------------------------------------------------------

// %APPDATA%\MU2 on Windows (the game's own user folder, core/paths.cpp), ~/Library/Application
// Support/MU2 on the Mac.
fs::path userFolder();
// Where the game goes unless the player picks another: %LOCALAPPDATA%\Programs\MU2 on Windows,
// which needs no administrator; ~/Applications on the Mac, where a MU2.app belongs (L5).
fs::path defaultInstall();

struct Settings {
    fs::path install;
    std::string email;  // the last one signed in with, filled in for him next time
};
Settings loadSettings();
void saveSettings(const Settings& settings);

// ---- the machine --------------------------------------------------------------------------------

struct Gpu;

// The graphics keys of the player's options.txt, from what the machine can draw (gpu.h): written
// when he has no options.txt of his own yet -- his first run -- and never over one he has, since
// his own rows in the game's Options write it back whole.
bool firstRunGraphics(const Gpu& gpu);

// The machine told to the site once a run (POST /api/machine, docs/plans/launcher.md, L7): the
// card, the driver, the memory and the OS -- the fleet the updates have to fit. Nothing about
// who is signed in: no email, no token. True when the report landed; only the log depends on it.
bool reportMachine(const Place& place, const Gpu& gpu, bool tuned, const std::string& token = {});

// ---- the release --------------------------------------------------------------------------------

struct Entry {
    std::string path;    // relative to the install, '/' between folders
    uint64_t size = 0;
    std::string sha256;
};

struct Manifest {
    std::string version;  // the commit it was built from
    int protocol = 0;
    std::string exe;
    uint64_t bytes = 0;
    std::vector<Entry> files;
    // Where the files are kept when not on the box: "https://.../files/", each file at its
    // sha256 under it (tools/mu/release.py). Empty: the site's own /game/<platform>/files/.
    std::string filesUrl;
};

bool fetchManifest(const Place& place, Manifest& out, std::string& error);

// How far the work has gone, read by the screens while a worker writes it.
struct Progress {
    std::atomic<uint64_t> done{0};   // bytes checked or fetched
    std::atomic<uint64_t> total{0};
    std::atomic<int> file{0};        // which of `files`
    std::atomic<int> files{0};
};

// The files of `manifest` the install lacks or holds otherwise. Each file's hash is taken only
// when its size or time differs from what the last update recorded (installed.json).
std::vector<Entry> stale(const fs::path& install, const Manifest& manifest, Progress& progress);

// Fetches `todo` into the install: each to <path>.part (resumed if a part is there), its hash
// checked, then put in place. Records the release in installed.json once all are in. `stop`, set
// from another thread, ends it between reads; the parts stay for next time.
bool update(const Place& place, const fs::path& install, const Manifest& manifest, const std::vector<Entry>& todo,
            Progress& progress, const std::atomic<bool>& stop, std::string& error);

// The release the install was last brought up to, or "".
std::string installedVersion(const fs::path& install);

// MU2.exe started in the install, signed in: the ticket and its secret in its environment as
// MU2_TICKET, never on its command line (game::accountKey). The token stays with the launcher.
// `pid`, when given, is the game's process, for handFront.
bool play(const fs::path& install, const Manifest& manifest, const GameTicket& ticket, std::string& error,
          unsigned long* pid = nullptr);

// **The game's window in front, handed by the launcher** (the user, 2026-10-15, on a Windows PC:
// the game still opened minimised behind everything). Windows lets a window come forward only
// from the program in front, and the launcher minimised itself the moment the game started, so
// by the time the game's window was up the front was someone else's and its own ask was refused.
// Waits, up to a minute, for the game's window to show, then puts it in front while the launcher
// still is; the caller minimises the launcher after. Returns at once on the Mac, or if the game
// exits first. Blocks: call it on a thread of its own.
void handFront(unsigned long pid);

// **The launcher at home in the install** (the user, 2026-10-15: "when mu is installed we need to
// move launcher to install folder so users is not launcher game from downloads folder", and "on
// install we need to create shortcut on desktop"). On Windows the running launcher is moved to
// <install>\MU2Launcher.exe -- a running program may be renamed on its own drive -- or copied
// there from another drive, and "MU Classic" is put on the desktop pointing at it: after a first
// install (`fresh`), or whenever the launcher has just been moved, since a shortcut made before
// then points at Downloads. A launcher already there, or a developer's (out/build), stays as it
// is; nothing on the Mac, where the launcher is an app kept where the player put it. Logged,
// never fatal: the game plays whatever this manages.
void settleLauncher(const fs::path& install, bool fresh);

// **Uninstall** (the user, 2026-10-15: "so we can try reinstall again"): the game's files --
// installed.json's list and the release's, so a half-made install goes too -- with each one's
// download left half done, installed.json, the folders that are empty after, and options.txt in
// the user folder, so the next install sets the graphics for this PC again. Kept: the launcher
// and its shortcut, so Install is one press away; the sign-in; logs and screenshots, which a
// report may still need. False, and `error` for the player, when a file would not go (on
// Windows, a game still running).
bool uninstall(const fs::path& install, const Manifest& manifest, std::string& error);

// Where crash and freeze reports wait to be sent (launcher/crash_upload.h): the game's logs/ in
// the install, and the user folder's, which holds the launcher's own and the game's where the
// install could not be written.
std::vector<fs::path> crashFolders(const fs::path& install);

}  // namespace mu::launcher
