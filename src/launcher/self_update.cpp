#include "launcher/self_update.h"

#include <atomic>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "core/json.h"
#include "core/log.h"
#include "launcher/https.h"
#include "launcher/sha256.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace mu::launcher {

namespace {

// How many times one release is tried before it is left alone (launcher-update.json).
constexpr int kMostTries = 2;

struct Newest {
    std::string version;     // the commit it was built from, for the log
    std::string exeSha;      // the program's own hash: what a current launcher's file hashes to
    std::string packageSha;  // what is downloaded: the exe itself on Windows, the app's tar.gz on the Mac
    uint64_t packageSize = 0;
};

std::string readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream all;
    all << in.rdbuf();
    return all.str();
}

}  // namespace

fs::path ownProgram() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
    buffer.resize(n);
    return fs::path(buffer);
#else
    char buffer[4096];
    uint32_t size = sizeof buffer;
    if (_NSGetExecutablePath(buffer, &size) != 0) return {};
    std::error_code ec;
    const fs::path real = fs::canonical(buffer, ec);
    return ec ? fs::path(buffer) : real;
#endif
}

// A developer's launcher, built in out/: never replaced by the site's.
bool developers(const fs::path& program) {
    const std::string p = utf8Of(program);
    return p.find("/out/build") != std::string::npos || p.find("\\out\\build") != std::string::npos;
}

namespace {

bool askNewest(const Place& place, Newest& out, std::string& note) {
    const Reply reply = request("GET", place.site + "/game/launcher-" + place.platform + "/manifest.json");
    if (reply.status != 200) {
        note = reply.status ? "no launcher release on the site (HTTP " + std::to_string(reply.status) + ")"
                            : "the site did not answer: " + reply.error;
        return false;
    }
    const core::Json said = core::parseJson(reply.body.data(), reply.body.size(), "the launcher's manifest");
    out.version = said["version"].stringOr("");
    out.exeSha = said["exe_sha256"].stringOr("");
    out.packageSha = said["package"]["sha256"].stringOr("");
    out.packageSize = uint64_t(said["package"]["size"].numberOr(0));
    if (out.exeSha.size() != 64 || out.packageSha.size() != 64 || out.packageSize == 0) {
        note = "the launcher's manifest made no sense";
        return false;
    }
    return true;
}

// launcher-update.json: which release was last tried, and how many times.
int triesOf(const std::string& target) {
    const std::string text = readAll(userFolder() / "launcher-update.json");
    if (text.empty()) return 0;
    const core::Json said = core::parseJson(text.data(), text.size(), "launcher-update.json");
    return said["target"].stringOr("") == target ? int(said["tries"].numberOr(0)) : 0;
}

void setTries(const std::string& target, int tries) {
    std::error_code ec;
    fs::create_directories(userFolder(), ec);
    std::ofstream out(userFolder() / "launcher-update.json", std::ios::binary | std::ios::trunc);
    out << "{\"target\": \"" << target << "\", \"tries\": " << tries << "}\n";
}

bool fetch(const Place& place, const Newest& newest, const fs::path& to, std::string& note) {
    std::error_code ec;
    fs::remove(to, ec);
    std::atomic<bool> stop{false};
    std::string error;
    const std::string url = place.site + "/game/launcher-" + place.platform + "/files/" + newest.packageSha;
    if (!download(url, to, false, {}, stop, error)) {
        note = "the new launcher did not download: " + error;
        return false;
    }
    if (fs::file_size(to, ec) != newest.packageSize || sha256File(to) != newest.packageSha) {
        note = "the new launcher came down wrong (size or hash)";
        fs::remove(to, ec);
        return false;
    }
    return true;
}

#ifdef _WIN32
std::wstring quotedArg(const std::wstring& s) { return L"\"" + s + L"\""; }

bool startProgram(const fs::path& exe, const std::vector<std::string>& args) {
    std::wstring line = quotedArg(exe.wstring());
    for (const std::string& a : args) line += L" " + quotedArg(pathOf(a).wstring());
    STARTUPINFOW start{};
    start.cb = sizeof start;
    PROCESS_INFORMATION running{};
    if (!CreateProcessW(exe.wstring().c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        exe.parent_path().wstring().c_str(), &start, &running)) {
        return false;
    }
    CloseHandle(running.hThread);
    CloseHandle(running.hProcess);
    return true;
}

fs::path oldOf(const fs::path& program) {
    return program.parent_path() / (program.stem().wstring() + L".old.exe");
}
#else
// The app bundle a program runs from: .../MU2Launcher.app/Contents/MacOS/MU2Launcher.
fs::path bundleOf(const fs::path& program) {
    const fs::path bundle = program.parent_path().parent_path().parent_path();
    return bundle.extension() == ".app" ? bundle : fs::path{};
}

bool run(const std::vector<std::string>& argv) {
    std::vector<char*> raw;
    std::vector<std::string> copy = argv;
    for (std::string& s : copy) raw.push_back(s.data());
    raw.push_back(nullptr);
    pid_t pid = 0;
    if (posix_spawn(&pid, raw[0], nullptr, nullptr, raw.data(), environ) != 0) return false;
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif

}  // namespace

SelfUpdate launcherCurrent(const Place& place, std::string& note) {
    Newest newest;
    if (!askNewest(place, newest, note)) return SelfUpdate::Skipped;
    const std::string mine = sha256File(ownProgram());
    if (mine == newest.exeSha) {
        note = "the newest, " + newest.version;
        return SelfUpdate::Current;
    }
    note = "older than the site's " + newest.version;
    return SelfUpdate::Skipped;
}

SelfUpdate selfUpdate(const Place& place, const std::vector<std::string>& args, std::string& note) {
    const fs::path program = ownProgram();
    if (program.empty()) {
        note = "cannot tell where this launcher is";
        return SelfUpdate::Skipped;
    }
    if (developers(program)) {
        note = "a developer's build";
        return SelfUpdate::Skipped;
    }
    Newest newest;
    if (!askNewest(place, newest, note)) return SelfUpdate::Skipped;
    if (sha256File(program) == newest.exeSha) {
        note = "the newest, " + newest.version;
        return SelfUpdate::Current;
    }
    const int tries = triesOf(newest.exeSha);
    if (tries >= kMostTries) {
        note = "launcher " + newest.version + " failed " + std::to_string(tries) + " times; left alone";
        return SelfUpdate::Skipped;
    }
    setTries(newest.exeSha, tries + 1);
    std::error_code ec;

#ifdef _WIN32
    // Beside it, under its own name: a browser may have saved it as "MU2Launcher (1).exe".
    const fs::path fresh = program.parent_path() / (program.stem().wstring() + L".update.exe");
    const fs::path old = oldOf(program);
    if (!fetch(place, newest, fresh, note)) return SelfUpdate::Skipped;
    fs::remove(old, ec);
    fs::rename(program, old, ec);  // a running exe may be renamed, not overwritten
    if (ec) {
        note = "cannot move this launcher aside: " + ec.message();
        fs::remove(fresh, ec);
        return SelfUpdate::Skipped;
    }
    fs::rename(fresh, program, ec);
    if (ec || !startProgram(program, args)) {
        note = "the new launcher would not take its place; the old one is back";
        std::error_code back;
        fs::remove(program, back);
        fs::rename(old, program, back);
        fs::remove(fresh, back);
        return SelfUpdate::Skipped;
    }
#else
    const fs::path bundle = bundleOf(program);
    if (bundle.empty()) {
        note = "not in an app bundle";
        return SelfUpdate::Skipped;
    }
    const fs::path where = bundle.parent_path();
    const fs::path tgz = where / ".MU2Launcher.update.tgz";
    const fs::path stage = where / ".MU2Launcher.update";
    const fs::path old = where / ".MU2Launcher.old.app";
    if (access(utf8Of(where).c_str(), W_OK) != 0) {
        note = "cannot write " + utf8Of(where);
        return SelfUpdate::Skipped;
    }
    if (!fetch(place, newest, tgz, note)) return SelfUpdate::Skipped;
    fs::remove_all(stage, ec);
    fs::create_directories(stage, ec);
    const fs::path fresh = stage / "MU2Launcher.app";
    if (!run({"/usr/bin/tar", "-xzf", utf8Of(tgz), "-C", utf8Of(stage)}) ||
        sha256File(fresh / "Contents" / "MacOS" / "MU2Launcher") != newest.exeSha) {
        note = "the new launcher did not unpack whole";
        fs::remove_all(stage, ec);
        fs::remove(tgz, ec);
        return SelfUpdate::Skipped;
    }
    fs::remove_all(old, ec);
    fs::rename(bundle, old, ec);
    if (ec) {
        note = "cannot move this launcher aside: " + ec.message();
        fs::remove_all(stage, ec);
        fs::remove(tgz, ec);
        return SelfUpdate::Skipped;
    }
    fs::rename(fresh, bundle, ec);
    std::vector<std::string> open = {"/usr/bin/open", "-n", utf8Of(bundle)};
    if (!args.empty()) {
        open.push_back("--args");
        open.insert(open.end(), args.begin(), args.end());
    }
    if (ec || !run(open)) {
        note = "the new launcher would not take its place; the old one is back";
        std::error_code back;
        fs::remove_all(bundle, back);
        fs::rename(old, bundle, back);
        fs::remove_all(stage, back);
        fs::remove(tgz, back);
        return SelfUpdate::Skipped;
    }
    fs::remove_all(stage, ec);
    fs::remove(tgz, ec);
#endif
    note = "updated to " + newest.version;
    core::logf("launcher: %s", note.c_str());
    return SelfUpdate::Started;
}

void cleanAfterSelfUpdate() {
    const fs::path program = ownProgram();
    if (program.empty()) return;
    std::error_code ec;
#ifdef _WIN32
    // The old one may still be closing: then it goes on the next start.
    fs::remove(oldOf(program), ec);
    fs::remove(program.parent_path() / (program.stem().wstring() + L".update.exe"), ec);
#else
    const fs::path bundle = bundleOf(program);
    if (bundle.empty()) return;
    fs::remove_all(bundle.parent_path() / ".MU2Launcher.old.app", ec);
    fs::remove_all(bundle.parent_path() / ".MU2Launcher.update", ec);
    fs::remove(bundle.parent_path() / ".MU2Launcher.update.tgz", ec);
#endif
}

}  // namespace mu::launcher
