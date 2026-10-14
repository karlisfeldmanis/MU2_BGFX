// MU2Launcher (docs/plans/launcher.md, L3). For now its engine with no screens: `--check` walks it
// end to end against a site, as the screens will.
//
//   mu2_launcher --check [--site URL] [--platform windows] [--dir DIR] [--only TEXT] [--play]
//
// The email and password come from MU2_EMAIL and MU2_PASSWORD, never the command line. --only
// keeps the files whose path holds TEXT, so a check need not fetch 1.5 GB.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "launcher/engine.h"

using namespace mu::launcher;

namespace {

int check(Place place, fs::path dir, const std::string& only, bool start) {
    auto step = [](const char* what, bool ok, const std::string& more = {}) {
        std::printf("%-28s %s%s%s\n", what, ok ? "ok" : "FAILED", more.empty() ? "" : "  ", more.c_str());
        return ok;
    };
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
    if (start) {
        if (!step("play", play(dir, manifest, back, error), error)) return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Place place;
    fs::path dir;
    std::string only;
    bool checking = false, start = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--check") checking = true;
        else if (a == "--play") start = true;
        else if (a == "--site" && i + 1 < argc) place.site = argv[++i];
        else if (a == "--platform" && i + 1 < argc) place.platform = argv[++i];
        else if (a == "--dir" && i + 1 < argc) dir = pathOf(argv[++i]);
        else if (a == "--only" && i + 1 < argc) only = argv[++i];
    }
    if (!checking) {
        std::fprintf(stderr, "usage: mu2_launcher --check [--site URL] [--platform P] [--dir DIR] [--only TEXT] [--play]\n");
        return 2;
    }
    return check(place, dir, only, start);
}
