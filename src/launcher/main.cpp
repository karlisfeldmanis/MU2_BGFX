// MU2Launcher (docs/plans/launcher.md, L3): its window (ui.cpp), or its engine walked end to end
// with no window (`--check`).
//
//   mu2_launcher [--site URL] [--platform P]                     the window
//   mu2_launcher --shot FILE --screen signin|error|install|downloading|ready   one screen, saved
//   mu2_launcher --check [--site URL] [--platform windows] [--dir DIR] [--only TEXT] [--play]
//
// The email and password come from MU2_EMAIL and MU2_PASSWORD, never the command line. --only
// keeps the files whose path holds TEXT, so a check need not fetch 1.5 GB.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/log.h"
#include "core/watch.h"
#include "launcher/crash_upload.h"
#include "launcher/engine.h"
#include "launcher/gpu.h"
#include "launcher/self_update.h"
#include "launcher/ui.h"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace mu::launcher;

namespace {

#ifdef _WIN32
// **One launcher at a time** (2026-10-15). Two at once lost the launcher's last log -- the second
// could not move the first's open launcher.log aside, so the digest sent home had no launcher in
// it -- and could fetch the same update twice. A launcher that has just replaced itself starts
// the new one while it is still closing, so the new one waits for it a while; one that is still
// there after that is a player's open launcher, brought forward, and this one leaves. The handle
// is held for the life of the process and let go by Windows at its end, however it ends.
bool onlyLauncher() {
    HANDLE one = CreateMutexW(nullptr, TRUE, L"Local\\MU2Launcher");
    if (!one || GetLastError() != ERROR_ALREADY_EXISTS) return true;
    const DWORD waited = WaitForSingleObject(one, 5000);
    if (waited == WAIT_OBJECT_0 || waited == WAIT_ABANDONED) return true;
    // The other launcher's window: a visible top-level window of a program named MU2Launcher*.
    EnumWindows(
        [](HWND w, LPARAM) -> BOOL {
            if (!IsWindowVisible(w) || GetWindow(w, GW_OWNER)) return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(w, &pid);
            if (pid == GetCurrentProcessId()) return TRUE;
            wchar_t exe[MAX_PATH] = {};
            DWORD size = MAX_PATH;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!process) return TRUE;
            const bool ours = QueryFullProcessImageNameW(process, 0, exe, &size) &&
                              std::wstring(exe).find(L"MU2Launcher") != std::wstring::npos;
            CloseHandle(process);
            if (!ours) return TRUE;
            if (IsIconic(w)) ShowWindow(w, SW_RESTORE);
            SetForegroundWindow(w);
            return FALSE;
        },
        0);
    return false;
}
#endif

int check(Place place, fs::path dir, const std::string& only, bool start) {
    auto step = [](const char* what, bool ok, const std::string& more = {}) {
        std::printf("%-28s %s%s%s\n", what, ok ? "ok" : "FAILED", more.empty() ? "" : "  ", more.c_str());
        return ok;
    };
    // The machine first: no network, and the first run's graphics are set here (gpu.h).
    // The launcher itself first: is it the site's newest (launcher/self_update.h)? Only said.
    {
        std::string note;
        const SelfUpdate is = launcherCurrent(place, note);
        step("launcher", is == SelfUpdate::Current, note);
    }
    const Gpu gpu = probeGpu();
    const bool tuned = firstRunGraphics(gpu);
    std::string seen = gpu.name.empty() ? "Graphics adapter" : gpu.name;
    if (!gpu.driver.empty()) seen += ", driver " + gpu.driver + (gpu.date.empty() ? "" : " of " + gpu.date);
    seen += gpu.vulkan ? ", vulkan yes" : ", vulkan MISSING";
    if (gpu.preset >= 0) seen += ", " + std::string(mu::core::kGraphicsPresets[gpu.preset].name) + " graphics";
    if (gpu.driverOld) seen += ", driver old";
    step("gpu", true, gpu.known ? seen : "no probe on this system");
    const bool have = fs::exists(userFolder() / "options.txt");
    const std::string graphics =
        tuned ? "first run set to " + std::string(mu::core::kGraphicsPresets[gpu.preset].name)
              : gpu.preset < 0 ? std::string("nothing to set")
              : have ? std::string("kept, options.txt is there")
                     : std::string("options.txt could not be written");
    step("graphics", tuned || gpu.preset < 0 || have, graphics);
    const bool sent = reportMachine(place, gpu, tuned);
    step("machine", sent || !gpu.known,
         sent ? "sent to the site" : gpu.known ? "could not send" : "no probe to send");
    Signed who;
    const char* email = std::getenv("MU2_EMAIL");
    const char* password = std::getenv("MU2_PASSWORD");
    if (email && password) {
        std::string message;
        const Answer a = signIn(place, email, password, who, message);
        if (!step("sign in", a == Answer::Ok, a == Answer::Ok ? who.email : message)) return 1;
        if (!step("keep the sign-in", keep(who))) return 1;
    }
    Signed back;
    if (!step("the sign-in kept", kept(back) && (who.token.empty() || back.token == who.token), back.email)) return 1;
    if (!step("still signed in", stillSigned(place, back) == Answer::Ok)) return 1;

    Manifest manifest;
    std::string error;
    const bool fetchedManifest = fetchManifest(place, manifest, error);
    if (!step("the manifest", fetchedManifest,
              fetchedManifest ? manifest.version + ", protocol " + std::to_string(manifest.protocol) + ", " +
                                    std::to_string(manifest.files.size()) + " files"
                              : error)) {
        return 1;
    }
    if (!only.empty()) {
        std::vector<Entry> keepers;
        manifest.bytes = 0;
        for (const Entry& e : manifest.files) {
            if (e.path.find(only) != std::string::npos) {
                keepers.push_back(e);
                manifest.bytes += e.size;
            }
        }
        manifest.files = keepers;
    }
    if (dir.empty()) dir = loadSettings().install;
    Progress progress;
    auto t0 = std::chrono::steady_clock::now();
    const std::vector<Entry> todo = stale(dir, manifest, progress);
    step("what is stale", true, std::to_string(todo.size()) + " of " + std::to_string(manifest.files.size()) + " in " + utf8Of(dir));
    std::atomic<bool> stop{false};
    t0 = std::chrono::steady_clock::now();
    const bool fetched = update(place, dir, manifest, todo, progress, stop, error);
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    char rate[96];
    std::snprintf(rate, sizeof rate, "%.1f MB in %.1f s", double(progress.total.load()) / 1e6, s);
    if (!step("update", fetched, fetched ? rate : error)) return 1;
    const std::vector<Entry> after = stale(dir, manifest, progress);
    if (!step("nothing stale after", after.empty(), "installed " + installedVersion(dir))) return 1;
    // What crashed or froze is sent home as the window sends it (launcher/crash_upload.h).
    const int reports = sendCrashes(crashFolders(dir), {place.site, place.platform, "launcher", back.token,
                                                     gpu.name, gpu.driver});
    step("crash reports", true, std::to_string(reports) + " sent");
    if (start) {
        GameTicket ticket;
        const bool ticketed = gameTicket(place, back, ticket, error) == Answer::Ok;
        if (!step("the game's ticket", ticketed, ticketed ? "ticket " + ticket.ticket.substr(0, 6) : error)) return 1;
        if (!step("play", play(dir, manifest, ticket, error), error)) return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Place place;
    fs::path dir;
    std::string only;
    bool checking = false, start = false, settling = false, removing = false, digesting = false;
    UiOptions ui;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--check") checking = true;
        else if (a == "--play") start = true;
        else if (a == "--settle") settling = true;
        else if (a == "--uninstall") removing = true;
        else if (a == "--digest") digesting = true;
        else if (a == "--site" && i + 1 < argc) place.site = argv[++i];
        else if (a == "--platform" && i + 1 < argc) place.platform = argv[++i];
        else if (a == "--dir" && i + 1 < argc) dir = pathOf(argv[++i]);
        else if (a == "--only" && i + 1 < argc) only = argv[++i];
        else if (a == "--shot" && i + 1 < argc) ui.shot = argv[++i];
        else if (a == "--screen" && i + 1 < argc) ui.shotScreen = argv[++i];
        else if (a == "--no-self-update") ui.selfUpdate = false;
    }
    // Handed on to a newer launcher: where it was pointed, and nothing else.
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if ((a == "--site" || a == "--platform") && i + 1 < argc) {
            ui.args.push_back(a);
            ui.args.push_back(argv[i + 1]);
        }
    }
    if (checking) return check(place, dir, only, start);
    // The install's own steps, with no site: --settle moves this launcher into --dir and puts the
    // shortcut on the desktop as a first install does; --uninstall removes what installed.json
    // lists there (engine.h). What a Wine run checks them by.
    // --digest: the log digest the launcher sends home (crash_upload.h), of --dir's logs/ and
    // the launcher's launcher.log, written there and not sent.
    if (digesting) {
        if (dir.empty()) dir = loadSettings().install;
        writeLogDigest({dir / "logs"}, dir / "logs" / "launcher.log", dir / "logs");
        return 0;
    }
    if (settling || removing) {
        if (dir.empty()) dir = loadSettings().install;
        if (settling) settleLauncher(dir, true);
        std::string error;
        if (removing && !uninstall(dir, Manifest{}, error)) {
            std::fprintf(stderr, "uninstall: %s\n", error.c_str());
            return 1;
        }
        return 0;
    }
    ui.place = place;
#ifdef _WIN32
    if (ui.shot.empty() && !onlyLauncher()) return 0;
#endif
    // The window's own log and black box, in the user folder: a launcher that dies after the
    // sign-in leaves launcher-crash-<when>.txt, sent on its next start.
    if (ui.shot.empty()) {
        const fs::path logs = userFolder() / "logs";
        std::error_code ec;
        fs::create_directories(logs, ec);
        mu::core::logOpen(utf8Of(logs / "launcher.log").c_str());
        mu::core::watchStart(utf8Of(logs).c_str(), "launcher-");
        mu::core::watchNote("MU2Launcher");
    }
    const int code = runUi(ui);
    mu::core::logClose();
    return code;
}
